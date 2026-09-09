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
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
#include <fstream>
#include <sstream>
#include <iomanip>
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
struct Case {std::string name,executor,dialog;bool hidden{};};
static Bytes readFile(const std::filesystem::path& path) {std::ifstream f(path,std::ios::binary);CHECK(f);return Bytes(std::istreambuf_iterator<char>(f),{});}
static std::string hex(const Digest& digest){std::ostringstream s;s<<std::hex<<std::setfill('0');for(auto b:digest)s<<std::setw(2)<<unsigned(b);return s.str();}
static std::uint32_t word(const Bytes& b,std::size_t at){CHECK(at+4<=b.size());return b[at]|std::uint32_t(b[at+1])<<8|std::uint32_t(b[at+2])<<16|std::uint32_t(b[at+3])<<24;}
static Case loadCase(const char* path,unsigned index,const std::filesystem::path& runtime) {
    auto bytes=readFile(std::filesystem::u8path(path));std::size_t at=0;
    auto number=[&]{auto n=word(bytes,at);at+=4;return n;};
    auto text=[&]{auto n=number();CHECK(n<=4096 && n<=bytes.size()-at);std::string value(bytes.begin()+at,bytes.begin()+at+n);at+=n;return value;};
    CHECK(number()==0x314c3357);Digest digest{};for(auto& b:digest){CHECK(at<bytes.size());b=bytes[at++];}
    CHECK(digest==Sha256(readFile(runtime/"WindBot"/"bots.json")));
    const auto count=number();CHECK(count>0 && count<=256 && index>0 && index<=count);Case selected;
    for(unsigned i=1;i<=count;++i){Case row{text(),text(),text(),false};CHECK(at<bytes.size() && bytes[at]<=1);row.hidden=bytes[at++]!=0;if(i==index)selected=row;}
    CHECK(at==bytes.size());return selected;
}
static void branch(const std::shared_ptr<const ResourceView>& resources,const std::wstring& executable,
                   const std::string& runtime,const Case& row,const std::filesystem::path& proofPath) {
    std::ofstream proof(proofPath,std::ios::binary);CHECK(proof);
    auto config=std::make_shared<RoomConfig>();config->resources=resources;
    config->capability.mode=RoomMode::LoopbackFree;config->capability.resources=resources->Fingerprint();
    // This local host harness uses a test-only consistent binding; its core and
    // bot are real. The driver records actual binary/source hashes separately.
    config->capability.engine[0]=1;config->botExecutable=executable;
    BotLaunchData launch;launch.runtimeRoot=runtime;launch.seed=83;launch.hand=3;
    launch.name=row.name;launch.executor=row.executor;launch.dialog=row.dialog;
    launch.engine=config->capability.engine;launch.resources=resources->Fingerprint();
    launch.cardView=CaptureBotCardView(*resources,dataManager,launch.engine);config->bot=launch;
    CardData normal{};CHECK(dataManager.GetData(48305365,&normal));
    CHECK(normal.level==4 && (normal.type&TYPE_NORMAL) && (normal.type&TYPE_MONSTER));
    const auto baseline=measurement::CountDescendants();
    {
        Listener listener;const auto session=NewSessionId();std::vector<Envelope> output;
        DuelPlayer human{};human.endpointId=1;human.undoPeer.ready=true;BufferIO::CopyCharArray(L"W3 history human",human.name);
        UndoDuel host(false,config,session,[&](DuelPlayer* player,const Envelope& envelope){CHECK(player==&human);output.push_back(envelope);return true;});
        host.host_info.duel_rule=5;host.host_info.start_lp=10000000;host.host_info.start_hand=5;
        host.host_info.time_limit=600;host.host_info.draw_count=1;host.host_info.no_check_deck=true;host.host_info.no_shuffle_deck=true;
        host.JoinGame(&human,nullptr,true);Bytes deck;BufferIO::VectorWrite<std::uint32_t>(deck,40);BufferIO::VectorWrite<std::uint32_t>(deck,0);
        for(unsigned i=0;i<40;++i)BufferIO::VectorWrite<std::uint32_t>(deck,48305365);
        host.UpdateDeck(&human,deck.data(),static_cast<unsigned>(deck.size()));host.PlayerReady(&human,true);
        pump(host,[&]{if(human.state!=CTOS_HAND_RESULT)host.StartDuel(&human);return human.state==CTOS_HAND_RESULT;});
        const auto selected=host.BotStatus()->selection;
        CHECK(selected.executor==row.executor && selected.name==row.name && selected.dialog==row.dialog);
        proof<<"executor="<<selected.executor<<"\ndeckRelativePath=Decks/"<<selected.deckFile<<".ydk\ndialog="<<selected.dialog<<"\nresources="<<hex(resources->Fingerprint())<<"\n";proof.flush();
        std::cout<<"real lobby ready executor="<<selected.executor<<" deck="<<selected.deckFile<<std::endl;
        host.HandResult(&human,1);pump(host,[&]{return human.state==CTOS_TP_RESULT;});host.TPResult(&human,1);
        std::uint64_t inputSequence{};std::size_t accepted{};
        auto submit=[&](Bytes response,Origin origin){
            CHECK(host.CurrentBoundary().checkpoint.player==human.type);const auto status=host.Status();
            for(const auto& e:EncodeGamePacket(session,host.InstalledEpoch(),status.prompt,++inputSequence,{CTOS_TIME_CONFIRM}))host.ReceiveUndo(&human,e);
            host.ReceiveUndo(&human,EncodeResponse({session,host.InstalledEpoch(),status.prompt,0,{}},origin,response));
            if(++accepted>2048)throw std::runtime_error("Human response fixture bound exceeded");
        };
        auto idle=[&]{
            for(unsigned i=0;i<256;++i){
                pump(host,[&]{return !host.HasActiveDuel() || (host.CurrentBoundary().kind==BoundaryKind::AwaitResponse &&
                    host.CurrentBoundary().checkpoint.player==human.type && host.Status().state==TxState::Running &&
                    !host.BotStatus()->humanPromptHeld && host.CurrentBoundary().checkpoint.aiLogCursor>0);});
                if(!host.HasActiveDuel())throw std::runtime_error("Duel ended before required human idle boundary: "+host.BotStatus()->failure);const auto p=host.CurrentBoundary().checkpoint.prompt;CHECK(p.size()>=2);
                std::cout<<"human prompt="<<unsigned(p[0])<<" epoch="<<host.InstalledEpoch()<<" history="<<host.History().Records().size()<<std::endl;
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
        proof<<"seed=";for(auto value:host.Initial().seed)proof<<value<<',';proof<<"\n";proof.flush();
        submit(integer(7),Origin::Manual);idle();
        submit(integer(7),Origin::Manual);idle();
        CHECK(std::any_of(host.History().Records().begin(),host.History().Records().end(),[](const ResponseRecord& r){return r.origin==Origin::Bot;}));
        std::cout<<"A two human end turns and real AI turns complete"<<std::endl;
        ClientField field;PlayerViewState view;ClientRestore client(field,view,human.type,session,0);
        std::size_t finalTarget{};
        for(unsigned undo=1;undo<=2;++undo){
            const auto targetIndex=host.History().Target(human.type);CHECK(targetIndex);finalTarget=*targetIndex;
            const auto target=host.History().Records().at(*targetIndex).before;
            host.ReceiveUndo(&human,{WireKind::Request,{session,host.InstalledEpoch(),host.Status().nextRequest,0,{}},{}});
            const auto key=host.ActiveKey();CHECK(host.Status().state==TxState::Preparing);
            FragmentAssembler assembler;for(const auto& e:output)if(e.kind==WireKind::Prepare && SameKey(e.key,key))assembler.Add(e.payload);
            const auto descriptor=DecodeRoomRestore(assembler.Finish());const auto visible=DecodePlayerRestore(descriptor.visible);
            {std::ofstream capture(proofPath.parent_path()/("restore-"+std::to_string(undo)+".bin"),std::ios::binary);capture.write(reinterpret_cast<const char*>(descriptor.visible.data()),descriptor.visible.size());}
            if(!client.Prepare(key,visible,target.prompt))throw std::runtime_error("N3 prepare: "+client.Error());host.ReceiveUndo(&human,{WireKind::Ready,key,{1}});
            pump(host,[&]{return host.Status().state==TxState::Committing;});
            CHECK(host.InstalledEpoch()==undo && host.History().Records().size()==*targetIndex);
            const auto before=host.History().Records().size();for(int i=0;i<10;++i){host.PollUndo();std::this_thread::sleep_for(std::chrono::milliseconds(2));}
            CHECK(host.Status().state==TxState::Committing && before==host.History().Records().size());
            CHECK(client.Commit(key,undo));Bytes epoch(8);epoch[0]=undo;host.ReceiveUndo(&human,{WireKind::CommitAck,key,epoch});
            pump(host,[&]{return host.Status().state==TxState::Running;});CHECK(client.Resume(key));inputSequence=0;idle();
            std::cout<<"actual N3 + private AI undo "<<undo<<" target="<<*targetIndex<<std::endl;
        }
        const auto prompt=host.CurrentBoundary().checkpoint.prompt;CHECK(prompt[0]==MSG_SELECT_IDLECMD && prompt.size()>2 && prompt[2]>0);
        submit(integer(0),Origin::Manual);idle(); // first listed normal summon (Axe Raider)
        CHECK(host.History().Records().size()>finalTarget && host.History().Records().at(finalTarget).response==integer(0));
        const auto beforeBot=std::count_if(host.History().Records().begin(),host.History().Records().end(),[](const ResponseRecord& r){return r.origin==Origin::Bot;});
        submit(integer(7),Origin::Automatic);idle();
        const auto afterBot=std::count_if(host.History().Records().begin(),host.History().Records().end(),[](const ResponseRecord& r){return r.origin==Origin::Bot;});CHECK(afterBot>beforeBot);
        for(const auto& e:EncodeGamePacket(session,host.InstalledEpoch(),host.Status().prompt,++inputSequence,{CTOS_SURRENDER}))host.ReceiveUndo(&human,e);
        CHECK(!host.HasActiveDuel());
        proof<<"twoUndo=pass\nbranchNormalSummon=pass\nsubsequentBotTurn=pass\ntermination=human-surrender\n";proof.flush();
        std::cout<<"PASS actual host/core two undo + changed summon + bot continuation + legal surrender: "<<row.executor<<std::endl;
    }
    CHECK(measurement::CountDescendants()==baseline);proof<<"history=pass\n";
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
    const auto row=loadCase(argv[3],std::stoul(argv[4]),root);branch(resources,executable,runtime,row,std::filesystem::u8path(argv[5]));
    WSACleanup();return 0;
} catch(const std::exception& e) {std::cerr<<"FAIL ai_host_history_tests: "<<e.what()<<std::endl;return 1;} }
