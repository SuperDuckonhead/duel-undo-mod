#include "test_support.h"
#include "undo_duel.h"
#include "data_manager.h"
#include "game.h"
#include "file_system.h"
#include <IFileArchive.h>
#include "netserver.h"
#include "mysocket.h"
#include "measurement_process.h"
#include "undo/host_bot_seat.h"
#include "undo/room_restore.h"
#include "undo/player_restore.h"
#include "undo/rebuilder.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <psapi.h>
namespace irr { namespace io { IFileSystem* createFileSystem(); } }
namespace ygo {
bool ClientField::OnEvent(const irr::SEvent&) { throw std::runtime_error("Unexpected GUI event in private AI host test"); }
void Game::AddDebugMsg(const char*) { throw std::runtime_error("Unexpected GUI log in private AI host test"); }
void DeckBuilder::RefreshPackListScroll() { throw std::runtime_error("Unexpected editor access in private AI host test"); }
}
using namespace ygo;
using namespace undo;
static Bytes integer(std::uint32_t n) {
    return {std::uint8_t(n),std::uint8_t(n>>8),std::uint8_t(n>>16),std::uint8_t(n>>24)};
}
static void alive(std::uint32_t pid) {
    measurement::Handle process(OpenProcess(SYNCHRONIZE,FALSE,pid));
    CHECK(pid && process.get() && WaitForSingleObject(process.get(),0)==WAIT_TIMEOUT);
}
template<class Predicate> static void pump(UndoDuel& host,Predicate predicate) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(90);
    while(!predicate() && std::chrono::steady_clock::now()<deadline) {
        host.PollUndo();
        if(host.Status().state==TxState::PausedFailed)throw std::runtime_error("Host paused: "+host.BotStatus()->failure);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(predicate());
}
// A real listener supplies the legacy StartDuel -> StopListen lifecycle. It has
// no gameplay clients: all host methods below remain on this test's owner thread.
struct Listener {
    Socket wake{};
    Listener() {
        unsigned short port{};CHECK(NetServer::StartServer(0,0x7f000001,&port,false));CHECK(port);
        wake=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);CHECK(wake!=INVALID_SOCKET);
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(0x7f000001);address.sin_port=htons(port);
        CHECK(connect(wake,reinterpret_cast<sockaddr*>(&address),sizeof address)==0);
    }
    ~Listener() {
        NetServer::StopServer();
        // Wake the existing event loop after its cross-thread stop request.
        shutdown(wake,SD_BOTH);CloseSocket(wake);
        for(int i=0;i<5000 && NetServer::IsRunning();++i) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
};
static std::uint32_t word(const Bytes& b,std::size_t at){CHECK(at+4<=b.size());return b[at]|std::uint32_t(b[at+1])<<8|std::uint32_t(b[at+2])<<16|std::uint32_t(b[at+3])<<24;}
static std::string hex(const Digest& digest){std::ostringstream s;s<<std::hex<<std::setfill('0');for(auto b:digest)s<<std::setw(2)<<unsigned(b);return s.str();}
static std::vector<DWORD> descendantPids(){
    measurement::Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));CHECK(snapshot.get()!=INVALID_HANDLE_VALUE);
    const auto children=measurement::ReadProcesses(snapshot.get());std::vector<DWORD> pending{GetCurrentProcessId()},result;std::set<DWORD> seen{GetCurrentProcessId()};
    while(!pending.empty()){const auto parent=pending.back();pending.pop_back();const auto range=children.equal_range(parent);for(auto item=range.first;item!=range.second;++item)if(seen.insert(item->second).second){result.push_back(item->second);pending.push_back(item->second);}}
    return result;
}
// Hold actual external bot resources read-only across every scale. Managed
// workers use the same share mode; writes/replacement are denied for this run.
struct BotResourceLease {
    std::string label;
    measurement::Handle handle;
    Digest digest{};
    BotResourceLease(const std::filesystem::path& root,const char* relative)
        :label(relative),handle(CreateFileW((root/relative).wstring().c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)) {
        CHECK(handle.get()!=INVALID_HANDLE_VALUE);LARGE_INTEGER size{};CHECK(GetFileSizeEx(handle.get(),&size) && size.QuadPart>=0 && size.QuadPart<=16*1024*1024);
        Bytes bytes(static_cast<std::size_t>(size.QuadPart));DWORD read{};
        CHECK(ReadFile(handle.get(),bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr) && read==bytes.size());digest=Sha256(bytes);
    }
};
using BotResources=std::vector<std::unique_ptr<BotResourceLease>>;struct Usage {std::uint64_t working{},privateBytes{},peakWorking{},peakPagefile{};DWORD handles{};};
static Usage usage(DWORD pid){
    measurement::Handle handle(pid==GetCurrentProcessId()?nullptr:OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,pid));
    HANDLE process=pid==GetCurrentProcessId()?GetCurrentProcess():handle.get();CHECK(process);
    PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof memory;CHECK(GetProcessMemoryInfo(process,reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof memory));
    Usage result{memory.WorkingSetSize,memory.PrivateUsage,memory.PeakWorkingSetSize,memory.PeakPagefileUsage};CHECK(GetProcessHandleCount(process,&result.handles));return result;
}
static void usageJson(std::ostream& out,DWORD pid,const Usage& value,const char* role="host"){
    out<<"{\"role\":\""<<role<<"\",\"pid\":"<<pid<<",\"workingSetBytes\":"<<value.working<<",\"privateBytes\":"<<value.privateBytes
       <<",\"lifetimePeakWorkingSetBytes\":"<<value.peakWorking<<",\"lifetimePeakPagefileBytes\":"<<value.peakPagefile<<",\"handleCount\":"<<value.handles<<'}';
}
static void branch(const std::shared_ptr<const ResourceView>& resources,const std::wstring& executable,
                   const std::string& runtime,const BotResources& botResources,std::size_t requested,unsigned repeats,std::ofstream& samples) {

    auto config=std::make_shared<RoomConfig>();config->resources=resources;
    config->capability.mode=RoomMode::LoopbackFree;config->capability.resources=resources->Fingerprint();
    // This local host harness uses a test-only consistent binding; its core and
    // bot are real. The driver records actual binary/source hashes separately.
    config->capability.engine[0]=1;config->botExecutable=executable;
    BotLaunchData launch;launch.runtimeRoot=runtime;launch.seed=83;launch.hand=3;
    launch.name="R2 ChainBurn";launch.executor="ChainBurn";launch.dialog="kiwi.zh-TW";
    launch.engine=config->capability.engine;launch.resources=resources->Fingerprint();
    launch.cardView=CaptureBotCardView(*resources,dataManager,launch.engine);config->bot=launch;
    CardData normal{};CHECK(dataManager.GetData(48305365,&normal));
    CHECK(normal.level==4 && (normal.type&TYPE_NORMAL) && (normal.type&TYPE_MONSTER));
    const auto baseline=measurement::CountDescendants(); const auto baselineHandles=usage(GetCurrentProcessId()).handles;
    {
        Listener listener;const auto session=NewSessionId();std::vector<Envelope> output;
        DuelPlayer human{};human.endpointId=1;human.undoPeer.ready=true;BufferIO::CopyCharArray(L"W3 history human",human.name);
        UndoDuel host(false,config,session,[&](DuelPlayer* player,const Envelope& envelope){CHECK(player==&human);output.push_back(envelope);return true;});
        host.host_info.duel_rule=5;host.host_info.start_lp=10000000;host.host_info.start_hand=5;
        host.host_info.time_limit=600;host.host_info.draw_count=0;host.host_info.no_check_deck=true;host.host_info.no_shuffle_deck=true;
        host.JoinGame(&human,nullptr,true);Bytes deck;BufferIO::VectorWrite<std::uint32_t>(deck,40);BufferIO::VectorWrite<std::uint32_t>(deck,0);
        for(unsigned i=0;i<40;++i)BufferIO::VectorWrite<std::uint32_t>(deck,48305365);
        host.UpdateDeck(&human,deck.data(),static_cast<unsigned>(deck.size()));host.PlayerReady(&human,true);
        pump(host,[&]{if(human.state!=CTOS_HAND_RESULT)host.StartDuel(&human);return human.state==CTOS_HAND_RESULT;});
        const auto selected=host.BotStatus()->selection;
        CHECK(selected.executor=="ChainBurn" && selected.name==launch.name && selected.dialog==launch.dialog && selected.deckFile=="AI_ChainBurn" && selected.hand==3 && selected.customDeckSource.empty());
        host.HandResult(&human,1);pump(host,[&]{return human.state==CTOS_TP_RESULT;});host.TPResult(&human,1);
        std::uint64_t inputSequence{};std::size_t accepted{};
        auto submit=[&](Bytes response,Origin origin){
            CHECK(host.CurrentBoundary().checkpoint.player==human.type);const auto status=host.Status();
            for(const auto& e:EncodeGamePacket(session,host.InstalledEpoch(),status.prompt,++inputSequence,{CTOS_TIME_CONFIRM}))host.ReceiveUndo(&human,e);
            host.ReceiveUndo(&human,EncodeResponse({session,host.InstalledEpoch(),status.prompt,0,{}},origin,response));
            if(++accepted>20000)throw std::runtime_error("Human response fixture bound exceeded");
        };
        auto idle=[&]{
            for(unsigned i=0;i<256;++i){
                pump(host,[&]{return !host.HasActiveDuel() || (host.CurrentBoundary().kind==BoundaryKind::AwaitResponse &&
                    host.CurrentBoundary().checkpoint.player==human.type && host.Status().state==TxState::Running &&
                    !host.BotStatus()->humanPromptHeld && host.CurrentBoundary().checkpoint.aiLogCursor>0);});
                if(!host.HasActiveDuel())throw std::runtime_error("Duel ended before required human idle boundary: "+host.BotStatus()->failure);const auto p=host.CurrentBoundary().checkpoint.prompt;CHECK(p.size()>=2);

                if(p[0]==MSG_SELECT_IDLECMD)return;
                if(p[0]==MSG_SELECT_CHAIN)submit(integer(0xffffffffu),Origin::Automatic);
                else if(p[0]==MSG_SELECT_YESNO || p[0]==MSG_SELECT_EFFECTYN || p[0]==MSG_SELECT_OPTION)submit(integer(0),Origin::Automatic);
                else if(p[0]==MSG_SELECT_CARD){
                    CHECK(p.size()>=6 && p.size()==6+std::size_t(p[5])*8 && p[3]<=p[5]);
                    Bytes response{p[3]};for(unsigned j=0;j<p[3];++j)response.push_back(j);submit(response,Origin::Automatic);
                }else if(p[0]==MSG_SELECT_PLACE || p[0]==MSG_SELECT_DISFIELD){
                    CHECK(p.size()==7);auto count=std::max<unsigned>(1,p[2]);const auto disabled=word(p,3);Bytes response;
                    for(unsigned bit=0;bit<32 && response.size()<count*3;++bit){
                        const auto slot=bit%8;if((bit%16<8 && slot>=7) || (disabled&(1u<<bit)))continue;
                        response.push_back(static_cast<std::uint8_t>(p[1]^(bit>=16)));response.push_back(bit%16<8?LOCATION_MZONE:LOCATION_SZONE);response.push_back(slot);
                    }
                    CHECK(response.size()==count*3);submit(response,Origin::Automatic);
                }else if(p[0]==MSG_SELECT_POSITION){CHECK(p.size()==7 && p[6]);submit(integer(p[6]&(~p[6]+1)),Origin::Automatic);}
                else throw std::runtime_error("Unsupported actual human prompt "+std::to_string(p[0]));
            }
            throw std::runtime_error("Human idle fixture transition bound exceeded");
        };
        idle();CHECK(human.type==0);
        while(host.History().Records().size()<requested){submit(integer(7),Origin::Automatic);idle();output.clear();}
        const auto steadyDescendants=measurement::CountDescendants();
        const auto fixedTarget=host.History().Records().size();const auto target=host.CurrentBoundary().checkpoint;
        samples<<"{\"kind\":\"fixture\",\"requestedResponses\":"<<requested<<",\"actualTargetResponses\":"<<fixedTarget
            <<",\"stableDescendantProcessCount\":"<<steadyDescendants<<",\"aiCursor\":"<<target.aiLogCursor<<",\"resourceDigest\":\""<<hex(resources->Fingerprint())<<"\",\"seed\":[";
        bool first=true;for(auto value:host.Initial().seed){if(!first)samples<<',';first=false;samples<<value;}
        samples<<"],\"cardViewSha256\":\""<<hex(Sha256(launch.cardView))<<"\",\"resolvedSelection\":{\"executor\":\"ChainBurn\",\"deckFile\":\"AI_ChainBurn\",\"dialog\":\"kiwi.zh-TW\",\"name\":\"R2 ChainBurn\",\"hand\":3,\"chat\":"<<(selected.chat?"true":"false")<<",\"usePreErrataEffects\":"<<(selected.usePreErrataEffects?"true":"false")<<"},\"botResources\":[";
        first=true;for(const auto& resource:botResources){if(!first)samples<<',';first=false;samples<<"{\"path\":\""<<resource->label<<"\",\"sha256\":\""<<hex(resource->digest)<<"\"}";}
        samples<<"]}\n";samples.flush();
        submit(integer(7),Origin::Manual);idle();
        const auto originalCount=host.History().Records().size();const auto originalCheckpoint=host.CurrentBoundary().checkpoint;
        ClientField field;PlayerViewState view;ClientRestore client(field,view,human.type,session,0);
        auto stable=[&]{const auto b=*host.BotStatus();return host.Status().state==TxState::Running && b.identity.state==BotState::Running &&
            !b.pendingInputs && !b.pendingOutputs && !b.fencePending && !b.humanPromptHeld && !b.candidatePid && !b.retainedPid;};
        auto trial=[&](bool failure,const char* kind,unsigned iteration){
            pump(host,[&]{return stable() && (host.Status().eligibleMask&(1u<<human.type));});
            CHECK(host.History().Target(human.type)==fixedTarget);
            const auto beforeCount=host.History().Records().size();const auto before=host.CurrentBoundary().checkpoint;
            const auto originalPid=host.BotStatus()->identity.activePid;const auto originalEpoch=host.InstalledEpoch();
            const auto started=std::chrono::steady_clock::now();std::uint64_t observedPrivate{},observedWorking{};
            auto sample=[&](const char* phase){
                const auto status=*host.BotStatus();const auto self=usage(GetCurrentProcessId());
                std::uint64_t totalPrivate=self.privateBytes,totalWorking=self.working;
                const auto descendants=descendantPids();const auto children=descendants.size();
                samples<<"{\"kind\":\""<<kind<<"\",\"phase\":\""<<phase<<"\",\"requestedResponses\":"<<requested
                    <<",\"actualTargetResponses\":"<<fixedTarget<<",\"liveResponses\":"<<host.History().Records().size()<<",\"iteration\":"<<iteration
                    <<",\"elapsedMs\":"<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()
                    <<",\"descendantProcessCount\":"<<children<<",\"activePid\":"<<status.identity.activePid<<",\"candidatePid\":"<<status.candidatePid<<",\"retainedPid\":"<<status.retainedPid<<",\"host\":";
                usageJson(samples,GetCurrentProcessId(),self);samples<<",\"actors\":[";bool comma=false;
                std::vector<DWORD> seen;
                for(auto pid:descendants){
                    seen.push_back(pid);const auto value=usage(pid);if(comma)samples<<',';comma=true;usageJson(samples,pid,value,pid==status.identity.activePid?"active":pid==status.candidatePid?"candidate":pid==status.retainedPid?"retained":"auxiliary");totalPrivate+=value.privateBytes;totalWorking+=value.working;
                }
                observedPrivate=std::max(observedPrivate,totalPrivate);observedWorking=std::max(observedWorking,totalWorking);
                samples<<"],\"aggregateCurrentPrivateBytes\":"<<totalPrivate<<",\"aggregateCurrentWorkingSetBytes\":"<<totalWorking
                    <<",\"observedPeakPrivateBytes\":"<<observedPrivate<<",\"observedPeakWorkingSetBytes\":"<<observedWorking<<"}\n";samples.flush();CHECK(samples);
            };
            output.clear();sample("before-request");
            host.ReceiveUndo(&human,{WireKind::Request,{session,originalEpoch,host.Status().nextRequest,0,{}},{}});
            const auto key=host.ActiveKey();CHECK(host.Status().state==TxState::Preparing);
            {
            FragmentAssembler fragments;for(const auto& envelope:output)if(envelope.kind==WireKind::Prepare && SameKey(envelope.key,key))fragments.Add(envelope.payload);
            auto visible=DecodePlayerRestore(DecodeRoomRestore(fragments.Finish()).visible);
            if(!client.Prepare(key,visible,visible.prompt))throw std::runtime_error("Actual N3 prepare: "+client.Error());
            }
            output.clear();
            pump(host,[&]{return host.BotStatus()->identity.state==BotState::Ready;});
            CHECK(host.BotStatus()->candidatePid && host.BotStatus()->identity.activePid==originalPid);sample("ai-ready-n3-prepared");
            if(failure){
                host.ReceiveUndo(&human,{WireKind::Ready,key,{0}});client.Abort(key);host.ReceiveUndo(&human,{WireKind::AbortAck,key,{}});
                pump(host,[&]{return stable() && (host.Status().eligibleMask&(1u<<human.type));});
                CHECK(host.InstalledEpoch()==originalEpoch && host.BotStatus()->identity.activePid==originalPid);
                CHECK(host.History().Records().size()==beforeCount && SamePosition(host.CurrentBoundary().checkpoint,before));alive(originalPid);
                sample("settled");
            }else{
                host.ReceiveUndo(&human,{WireKind::Ready,key,{1}});
                pump(host,[&]{return host.Status().state==TxState::Committing && host.BotStatus()->identity.state==BotState::Committed;});
                CHECK(host.History().Records().size()==fixedTarget && host.BotStatus()->retainedPid==originalPid);sample("committed-awaiting-human-ack");
                const auto epoch=host.InstalledEpoch();CHECK(epoch==originalEpoch+1 && client.Commit(key,epoch));Bytes ack;for(unsigned i=0;i<8;++i)ack.push_back(epoch>>(8*i));
                host.ReceiveUndo(&human,{WireKind::CommitAck,key,ack});pump(host,stable);CHECK(client.Resume(key));
                CHECK(host.BotStatus()->identity.activePid!=originalPid && !host.BotStatus()->retainedPid);inputSequence=0;
                sample("settled");
            }
            CHECK(measurement::CountDescendants()==steadyDescendants);
            std::cout<<kind<<" requested="<<requested<<" actualTarget="<<fixedTarget<<" iteration="<<iteration<<" elapsedMs="
                <<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()<<std::endl;
        };
        // Warm both install and abort paths before the stable handle baseline.
        trial(false,"warm-success",0);submit(integer(7),Origin::Manual);idle();
        trial(true,"warm-failure",0);
        const auto steadyHandles=usage(GetCurrentProcessId()).handles;
        auto recordContinuation=[&](const char* kind,unsigned iteration){
            samples<<"{\"kind\":\"continuation\",\"transactionKind\":\""<<kind<<"\",\"requestedResponses\":"<<requested
                <<",\"iteration\":"<<iteration<<",\"baselineHostHandles\":"<<steadyHandles<<",\"host\":";
            usageJson(samples,GetCurrentProcessId(),usage(GetCurrentProcessId()));samples<<"}\n";samples.flush();CHECK(samples);
        };        for(unsigned i=1;i<=repeats;++i){
            trial(false,"success",i);submit(integer(7),Origin::Manual);idle();pump(host,stable);
            CHECK(host.History().Records().size()==originalCount && SamePosition(host.CurrentBoundary().checkpoint,originalCheckpoint));
            recordContinuation("success",i);
        }
        for(unsigned i=1;i<=repeats;++i){
            trial(true,"failure",i);
            // A real accepted continuation proves the original core+PID usable.
            submit(integer(7),Origin::Automatic);idle();CHECK(host.History().Records().size()>originalCount);
            // Explicitly separate cleanup successes from the measured 20 successes:
            // rewind to the same Manual target and replay A to restore its baseline.
            trial(false,"cleanup-success",i);submit(integer(7),Origin::Manual);idle();pump(host,stable);
            CHECK(host.History().Records().size()==originalCount && SamePosition(host.CurrentBoundary().checkpoint,originalCheckpoint));
            recordContinuation("cleanup-success",i);
        }
    }
    CHECK(measurement::CountDescendants()==baseline);
    samples<<"{\"kind\":\"scale-complete\",\"requestedResponses\":"<<requested<<",\"descendantProcessCount\":"<<measurement::CountDescendants()<<",\"initialHostHandles\":"<<baselineHandles<<",\"finalHostHandles\":"<<usage(GetCurrentProcessId()).handles<<"}\n";samples.flush();
}
int main(int argc,char** argv) {try {
    CHECK(argc==6);WSADATA winsock;CHECK(WSAStartup(MAKEWORD(2,2),&winsock)==0);
    std::unique_ptr<irr::io::IFileSystem,void(*)(irr::io::IFileSystem*)> files(irr::io::createFileSystem(),[](auto* p){p->drop();});
    dataManager.IrrFileSystem=files.get();
    CHECK(dataManager.LoadDB((std::filesystem::u8path(argv[2])/"cards.cdb").u8string().c_str()));
    const auto root=std::filesystem::u8path(argv[2]);
    // Load the same merged DataManager view once, then bind engine and bot to it.
    FileSystem::TraversalDir((root/"expansions").u8string().c_str(),[&](const char* name,bool directory) {
        if(directory)return;
        const auto path=root/"expansions"/std::filesystem::u8path(name);
        const auto extension=path.extension().u8string();
        if(extension==".cdb") CHECK(dataManager.LoadDB(path.u8string().c_str()));
        else if(extension==".zip" || extension==".ypk") CHECK(files->addFileArchive(path.u8string().c_str(),true,false,irr::io::EFAT_ZIP));
    });
    for(irr::u32 i=0;i<files->getFileArchiveCount();++i) {
        auto* list=files->getFileArchive(i)->getFileList();
        for(irr::u32 j=0;j<list->getFileCount();++j) {
            const auto name=list->getFullFileName(j);
            if(std::filesystem::u8path(name.c_str()).extension()==".cdb")CHECK(dataManager.LoadDB(name.c_str()));
        }
    }
            bool prefer=false;
    for(const char* config:{"system.conf","load-once.conf"}){std::ifstream file(root/config);std::string line;while(std::getline(file,line)){int value;if(std::sscanf(line.c_str(),"prefer_expansion_script = %d",&value)==1)prefer=value!=0;}}
    auto resources=ResourceView::Capture(argv[2],dataManager,prefer);
    const auto executable=std::filesystem::absolute(std::filesystem::u8path(argv[1])).wstring();
    const auto runtime=(std::filesystem::u8path(argv[2])/"WindBot").u8string();
    BotResources botResources;for(const char* path:{"bots.json","Decks/AI_ChainBurn.ydk","Dialogs/kiwi.zh-TW.json"})botResources.push_back(std::make_unique<BotResourceLease>(std::filesystem::u8path(runtime),path));
    std::vector<std::size_t> scales;std::stringstream cases(argv[3]);std::string text;
    while(std::getline(cases,text,',')){std::size_t used{};const auto n=std::stoul(text,&used);CHECK(used==text.size() && n>0 && n<=10000);scales.push_back(n);}
    CHECK(!scales.empty());std::sort(scales.begin(),scales.end());CHECK(std::adjacent_find(scales.begin(),scales.end())==scales.end());
    std::size_t used{};const auto repeats=std::stoul(argv[5],&used);CHECK(used==std::string(argv[5]).size() && repeats>0 && repeats<=100);
    std::ofstream samples(std::filesystem::u8path(argv[4]),std::ios::binary);CHECK(samples);samples<<std::setprecision(12);
    for(auto scale:scales)branch(resources,executable,runtime,botResources,scale,static_cast<unsigned>(repeats),samples);
    WSACleanup();return 0;
} catch(const std::exception& e) {std::cerr<<"FAIL undo_host_measure_tests: "<<e.what()<<std::endl;return 1;} }
