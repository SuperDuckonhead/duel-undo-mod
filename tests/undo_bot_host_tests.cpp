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
struct SuspendedProcess {
    std::vector<HANDLE> threads;
    explicit SuspendedProcess(DWORD pid) {
        measurement::Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0));
        THREADENTRY32 entry{};entry.dwSize=sizeof entry;CHECK(Thread32First(snapshot.get(),&entry));
        do {if(entry.th32OwnerProcessID==pid) {
            HANDLE thread=OpenThread(THREAD_SUSPEND_RESUME,FALSE,entry.th32ThreadID);CHECK(thread);
            if(SuspendThread(thread)==DWORD(-1)){CloseHandle(thread);throw std::runtime_error("SuspendThread failed");}
            threads.push_back(thread);
        }} while(Thread32Next(snapshot.get(),&entry));
        CHECK(!threads.empty());
    }
    ~SuspendedProcess(){for(auto thread:threads){ResumeThread(thread);CloseHandle(thread);}}
};
static void terminateControl(DWORD active) {
    measurement::Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));
    const auto processes=measurement::ReadProcesses(snapshot.get());DWORD control{};
    for(const auto& entry:processes)if(entry.second==active)control=entry.first;
    CHECK(control && control!=GetCurrentProcessId());
    bool owned=false;for(const auto& entry:processes)if(entry.first==GetCurrentProcessId() && entry.second==control)owned=true;
    CHECK(owned);measurement::Handle process(OpenProcess(PROCESS_TERMINATE|SYNCHRONIZE,FALSE,control));
    CHECK(process.get() && TerminateProcess(process.get(),99));CHECK(WaitForSingleObject(process.get(),5000)==WAIT_OBJECT_0);
}
template<class Predicate> static void pump(UndoDuel& host,Predicate predicate) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(40);
    while(!predicate() && std::chrono::steady_clock::now()<deadline) {
        host.PollUndo();
        if(host.Status().state==TxState::PausedFailed)throw std::runtime_error("Host paused: "+host.BotStatus()->failure);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(predicate());
}
static BotCompletion initialized(HostBotSeat& seat) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(40);
    while(std::chrono::steady_clock::now()<deadline) {
        for(auto& result:seat.Poll()) if(result.operation==BotOperation::Initialize) return result;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    throw std::runtime_error("Private AI process initialization timed out");
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
static void branch(const std::shared_ptr<const ResourceView>& resources,
                   const std::wstring& executable,const std::string& runtime,const std::string& executor,const std::string& mode="success") {
    auto config=std::make_shared<RoomConfig>();config->resources=resources;
    config->capability.mode=RoomMode::LoopbackFree;
    config->capability.resources=resources->Fingerprint();config->capability.engine[0]=1;
    config->botExecutable=executable;
    BotLaunchData launch;launch.runtimeRoot=runtime;launch.seed=83;
    launch.engine=config->capability.engine;launch.resources=resources->Fingerprint();
    launch.cardView=CaptureBotCardView(*resources,dataManager,launch.engine);
    launch.selectionCommand="Name='Actual Private AI' Deck="+executor+" Hand=3 Chat=true";
    config->bot=launch;
    const auto baseline=measurement::CountDescendants();
    // Distinguish an unsupported host from a broken executable/resource fixture.
    // This preflight runs the actual independently spawned worker and is stopped
    // before the host takes ownership; it never supplies decisions to the duel.
    {
        HostBotSeat seat(executable,launch,NewSessionId(),0,1);
        const auto result=initialized(seat);
        CHECK(result.accepted && result.selection.executor==executor);
        CHECK(result.selection.name=="Actual Private AI" && result.selection.hand==3);
        alive(result.identity.activePid);
        std::cout<<"PASS real private-process preflight "<<executor<<" PID="<<result.identity.activePid<<std::endl;
        seat.Stop();
    }
    CHECK(measurement::CountDescendants()==baseline);
    {
        Listener listener;
        const auto session=NewSessionId();
        std::vector<Envelope> output;
        GamePacketStream queryStream(session,0);bool invalidFieldQuery=false,invalidCardQuery=false;
        auto readWord=[](const Bytes& b,std::size_t at){CHECK(at+4<=b.size());return std::uint32_t(b[at])|std::uint32_t(b[at+1])<<8|std::uint32_t(b[at+2])<<16|std::uint32_t(b[at+3])<<24;};
        DuelPlayer human{};human.endpointId=1;human.undoPeer.ready=true;
        BufferIO::CopyCharArray(L"Actual Human Host",human.name);
        UndoDuel host(false,config,session,[&](DuelPlayer* player,const Envelope& envelope) {
            CHECK(player==&human); // AI readiness never comes from this callback.
            output.push_back(envelope);
            if(envelope.kind==WireKind::Game && envelope.key.epoch==0) {
                auto packet=queryStream.Add(envelope);
                if(packet && packet->packet.size()>4 && packet->packet[0]==STOC_GAME_MSG) {
                    const auto& bytes=packet->packet;const auto opcode=bytes[1];
                    if(opcode==MSG_UPDATE_DATA || opcode==MSG_UPDATE_CARD) {
                        for(std::size_t at=opcode==MSG_UPDATE_DATA?4:5;at<bytes.size();) {
                            const auto length=readWord(bytes,at);CHECK(length>=4 && length<=bytes.size()-at);
                            if(length>=8 && (readWord(bytes,at+4)&~0xefffffU)) {
                                if(opcode==MSG_UPDATE_DATA)invalidFieldQuery=true;else invalidCardQuery=true;
                            }
                            at+=length;
                        }
                    }
                }
            }
            return true;
        });
        host.host_info.duel_rule=5;host.host_info.start_lp=80000;host.host_info.start_hand=5;
        host.host_info.time_limit=60;host.host_info.draw_count=1;
        host.host_info.no_check_deck=true;host.host_info.no_shuffle_deck=true;
        host.JoinGame(&human,nullptr,true);
        Bytes deck;BufferIO::VectorWrite<std::uint32_t>(deck,40);BufferIO::VectorWrite<std::uint32_t>(deck,0);
        for(unsigned i=0;i<40;++i)BufferIO::VectorWrite<std::uint32_t>(deck,70368879); // public Upstart Goblin
        host.UpdateDeck(&human,deck.data(),static_cast<unsigned>(deck.size()));host.PlayerReady(&human,true);
        // Start succeeds only after actual OnJoinGame/OnTypeChange output has
        // passed UpdateDeck/PlayerReady; there is no fabricated bot deck/Ready.
        pump(host,[&] {if(human.state!=CTOS_HAND_RESULT)host.StartDuel(&human);return human.state==CTOS_HAND_RESULT;});
        std::cout<<"stage real lobby/RPS ready"<<std::endl;
        host.HandResult(&human,1); // beats the actual configured Hand=3 callback
        pump(host,[&] {return human.state==CTOS_TP_RESULT;});
        const auto originalPid=host.BotStatus()->identity.activePid;alive(originalPid);
        {
            SuspendedProcess blocked(originalPid);
            host.TPResult(&human,1);
            // Actual legacy refresh masks carry an undefined compatibility bit.
            // Exercise both virtual query bridges while the first boundary is held.
            host.RefreshHand(human.type,0x781fff,0);
            host.RefreshSingle(human.type,LOCATION_HAND,0,0xf81fff);
            CHECK(host.BotStatus()->humanPromptHeld && host.BotStatus()->fencePending);
            const auto heldCount=output.size();const auto frozenClock=host.Status().clock.remainingMs;
            for(int i=0;i<20;++i){host.TimerTick();std::this_thread::sleep_for(std::chrono::milliseconds(2));}
            CHECK(host.BotStatus()->humanPromptHeld && output.size()==heldCount && host.History().Records().empty());
            CHECK(host.Status().clock.remainingMs==frozenClock && host.Status().timePlayer==2);
            host.ReceiveUndo(&human,EncodeResponse({session,0,host.Status().prompt,0,{}},Origin::Manual,integer(7)));
            CHECK(host.History().Records().empty());
            const TxKey heldRequest{session,0,host.Status().nextRequest,0,{}};
            const auto beforeRejection=output.size();
            host.ReceiveUndo(&human,{WireKind::Request,heldRequest,{}});
            CHECK(output.size()==beforeRejection+1 && output.back().kind==WireKind::RequestRejected &&
                  SameKey(output.back().key,heldRequest) && output.back().payload==Bytes{1});
            CHECK(host.Status().clock.remainingMs==frozenClock && host.History().Records().empty());
        }
        std::cout<<"stage TP core="<<host.HasActiveDuel()<<" failure="<<host.BotStatus()->failure<<std::endl;
        std::uint64_t inputSequence{};
        auto submit=[&](Bytes response,Origin origin) {
            CHECK(host.CurrentBoundary().checkpoint.player==human.type);
            const auto status=host.Status();
            for(const auto& e:EncodeGamePacket(session,host.InstalledEpoch(),status.prompt,++inputSequence,{CTOS_TIME_CONFIRM}))host.ReceiveUndo(&human,e);
            host.ReceiveUndo(&human,EncodeResponse({session,host.InstalledEpoch(),status.prompt,0,{}},origin,response));
        };
        auto idle=[&] {
            for(unsigned i=0;i<100;++i) {
                pump(host,[&] {return host.HasActiveDuel() && host.CurrentBoundary().kind==BoundaryKind::AwaitResponse &&
                    host.CurrentBoundary().checkpoint.player==human.type && host.Status().state==TxState::Running &&
                    host.CurrentBoundary().checkpoint.aiLogCursor>0;});
                const auto prompt=host.CurrentBoundary().checkpoint.prompt.at(0);
                if(prompt==MSG_SELECT_IDLECMD)return;
                if(prompt==MSG_SELECT_CHAIN)submit(integer(0xffffffffu),Origin::Automatic);
                else if(prompt==MSG_SELECT_PLACE) {
                    const auto bytes=host.CurrentBoundary().checkpoint.prompt;
                    CHECK(bytes.size()==7 && bytes[2]<=1);
                    const std::uint32_t disabled=bytes[3]|std::uint32_t(bytes[4])<<8|std::uint32_t(bytes[5])<<16|std::uint32_t(bytes[6])<<24;
                    unsigned slot=0;while(slot<5 && (disabled&(1u<<(8+slot))))++slot;CHECK(slot<5);
                    submit({human.type,LOCATION_SZONE,std::uint8_t(slot)},Origin::Automatic);
                } else throw std::runtime_error("Unexpected human prompt: "+std::to_string(prompt));
            }
            throw std::runtime_error("Actual human idle prompt did not arrive");
        };
        idle();std::cout<<"stage first human idle"<<std::endl;const auto target=host.CurrentBoundary().checkpoint;
        const auto nativeAllowance=host.Status().clock.remainingMs[human.type];
        host.TimerTick();CHECK(host.Status().clock.remainingMs[human.type]==nativeAllowance-1000);
        if(invalidFieldQuery || invalidCardQuery)std::cerr<<"undefined query bit: field="<<invalidFieldQuery<<" card="<<invalidCardQuery<<std::endl;
        CHECK(!invalidFieldQuery && !invalidCardQuery);
        CHECK(target.aiLogCursor>0 && target.aiLogCursor==host.BotStatus()->fencedCursor &&
              target.aiLogCursor==host.BotStatus()->cursor && !host.BotStatus()->fencePending &&
              !host.BotStatus()->pendingInputs && !host.BotStatus()->pendingOutputs);
        // A real rejected human response must keep the host-only AI fence.
        const auto retryPrompt=host.Status().prompt;
        submit(integer(0x7fffffffu),Origin::Manual);
        CHECK(host.CurrentBoundary().rejectedResponse && host.Status().prompt==retryPrompt && host.History().Records().empty());
        CHECK(human.state==CTOS_TIME_CONFIRM);
        CHECK(host.CurrentBoundary().checkpoint.aiLogCursor==target.aiLogCursor);
        CHECK(host.CurrentBoundary().checkpoint.prompt==target.prompt && host.BotStatus()->fencedCursor==target.aiLogCursor);
        const TxKey unavailable{session,0,host.Status().nextRequest,0,{}};
        const auto beforeUnavailable=output.size();const auto unavailableClock=host.Status().clock.remainingMs;
        host.ReceiveUndo(&human,{WireKind::Request,unavailable,{}});
        CHECK(output.size()==beforeUnavailable+1 && output.back().kind==WireKind::RequestRejected && output.back().payload==Bytes{2});
        CHECK(host.Status().clock.remainingMs==unavailableClock && host.Status().nextRequest==unavailable.request);
        submit(integer(7),Origin::Manual);idle();std::cout<<"stage actual bot turn complete"<<std::endl;
        CHECK(std::any_of(host.History().Records().begin(),host.History().Records().end(),[](const ResponseRecord& r){return r.origin==Origin::Bot;}));
        auto targetIndex=host.History().Target(human.type);CHECK(targetIndex);
        CHECK(host.History().Records().at(*targetIndex).before.aiLogCursor==target.aiLogCursor);
        const TxKey request{session,host.InstalledEpoch(),host.Status().nextRequest,0,{}};
        host.ReceiveUndo(&human,{WireKind::Request,request,{}});
        const auto key=host.ActiveKey();CHECK(host.Status().state==TxState::Preparing);
        const auto activeClock=host.Status().clock.remainingMs;
        for(unsigned i=0;i<5;++i)host.TimerTick();
        CHECK(host.Status().clock.remainingMs==activeClock);
        const auto beforeBusy=output.size();
        auto competitor=request;++competitor.request;
        host.ReceiveUndo(&human,{WireKind::Request,competitor,{}});
        CHECK(output.size()==beforeBusy+1 && output.back().kind==WireKind::RequestRejected &&
              SameKey(output.back().key,competitor) && output.back().payload==Bytes{1});
        CHECK(SameKey(host.ActiveKey(),key) && host.Status().state==TxState::Preparing && host.Status().clock.remainingMs==activeClock);
        auto malformed=competitor;malformed.targetIndex=1;const auto beforeMalformed=output.size();
        host.ReceiveUndo(&human,{WireKind::Request,malformed,{}});CHECK(output.size()==beforeMalformed);
        const auto beforeDuplicate=output.size();host.ReceiveUndo(&human,{WireKind::Request,request,{}});
        CHECK(output.size()>beforeDuplicate && std::none_of(output.begin()+beforeDuplicate,output.end(),[](const Envelope& e){return e.kind==WireKind::RequestRejected;}));
        FragmentAssembler assembler;
        // Duplicate Prepare fragments are separately checked above; decode the original stream.
        for(std::size_t i=0;i<beforeBusy;++i){const auto& e=output[i];if(e.kind==WireKind::Prepare && SameKey(e.key,key))assembler.Add(e.payload);}
        const auto descriptor=DecodeRoomRestore(assembler.Finish());
        auto visible=DecodePlayerRestore(descriptor.visible);
        ClientField field;PlayerViewState view;
        ClientRestore client(field,view,human.type,session,0);
        CHECK(client.Prepare(key,visible,target.prompt)); // actual N3 candidate model
        // Candidate PID/Ready comes only from the real private pipe participant.
        pump(host,[&]{return host.BotStatus()->identity.state==BotState::Ready;});
        const auto readyBot=*host.BotStatus();
        CHECK(readyBot.identity.activePid==originalPid && readyBot.candidatePid && readyBot.candidatePid!=originalPid);
        alive(originalPid);alive(readyBot.candidatePid);
        CHECK(host.InstalledEpoch()==0 && readyBot.cursor>=target.aiLogCursor);
        host.ReceiveUndo(&human,{WireKind::Ready,key,{1}});
        pump(host,[&] {return host.Status().state==TxState::Committing;});
        CHECK(host.InstalledEpoch()==1 && host.History().Records().size()==*targetIndex);
        // The human's actual client install is withheld: AI's own ack is
        // insufficient to release any response or resume the candidate process.
        const auto frozenHistory=host.History().Records().size();
        for(int i=0;i<20;++i){host.PollUndo();std::this_thread::sleep_for(std::chrono::milliseconds(2));}
        CHECK(host.Status().state==TxState::Committing && host.History().Records().size()==frozenHistory);
        pump(host,[&]{return host.BotStatus()->identity.state==BotState::Committed;});
        const auto committedBot=*host.BotStatus();
        CHECK(committedBot.identity.activePid==readyBot.candidatePid && committedBot.retainedPid==originalPid && committedBot.commitCount==1);
        alive(originalPid);alive(committedBot.identity.activePid);
        CHECK(std::none_of(output.begin(),output.end(),[&](const Envelope& e){return e.kind==WireKind::Resume && SameKey(e.key,key);}));
        host.ReceiveUndo(&human,{WireKind::Ready,key,{1}}); // duplicate does not install twice
        CHECK(host.BotStatus()->commitCount==1);
        if(mode=="missing-ack") {
            const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(33);
            while(host.Status().state!=TxState::PausedFailed && std::chrono::steady_clock::now()<until) {
                host.PollUndo();std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            CHECK(host.Status().state==TxState::PausedFailed && host.InstalledEpoch()==1);
            CHECK(host.History().Records().size()==frozenHistory && host.BotStatus()->retainedPid==originalPid);
            alive(originalPid);alive(committedBot.identity.activePid);
            CHECK(std::none_of(output.begin(),output.end(),[&](const Envelope& e){return e.kind==WireKind::Resume && SameKey(e.key,key);}));
            std::cout<<"PASS actual AI committed + missing human Ack leaves both processes retained and paused after deadline"<<std::endl;
        } else {
        CHECK(client.Commit(key,1));Bytes epoch(8);epoch[0]=1;
        if(mode=="broken-pipe")terminateControl(committedBot.identity.activePid);
        host.ReceiveUndo(&human,{WireKind::CommitAck,key,epoch});
        if(mode=="broken-pipe") {
            const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(10);
            while(host.Status().state!=TxState::PausedFailed && std::chrono::steady_clock::now()<until) {
                host.PollUndo();std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            CHECK(host.Status().state==TxState::PausedFailed && host.InstalledEpoch()==1);
            CHECK(host.History().Records().size()==frozenHistory && !host.BotStatus()->failure.empty());
            CHECK(std::none_of(output.begin(),output.end(),[&](const Envelope& e){return e.kind==WireKind::Resume && SameKey(e.key,key);}));
            std::cout<<"PASS actual broken control pipe during Resume leaves uncertain commit paused without human Resume"<<std::endl;
        } else {
        pump(host,[&] {return host.Status().state==TxState::Running;});CHECK(client.Resume(key));
        CHECK(host.BotStatus()->retainedPid==0 && host.BotStatus()->identity.state==BotState::Running);
        CHECK(host.BotStatus()->identity.activePid==readyBot.candidatePid && host.BotStatus()->identity.epoch==1);
        BotOutput token{session,1,host.Status().prompt,Origin::Bot,readyBot.candidatePid,{CTOS_RESPONSE}};
        CHECK(AcceptsBotOutput(token,host.BotStatus()->identity,host.Status().prompt));
        auto stale=token;stale.producerPid=originalPid;CHECK(!AcceptsBotOutput(stale,host.BotStatus()->identity,host.Status().prompt));
        stale=token;stale.epoch=0;CHECK(!AcceptsBotOutput(stale,host.BotStatus()->identity,host.Status().prompt));
        stale=token;++stale.prompt;CHECK(!AcceptsBotOutput(stale,host.BotStatus()->identity,host.Status().prompt));
        stale=token;stale.session[0]^=1;CHECK(!AcceptsBotOutput(stale,host.BotStatus()->identity,host.Status().prompt));
        const auto retainedSize=host.History().Records().size();
        host.ReceiveUndo(&human,EncodeResponse({session,0,host.Status().prompt,0,{}},Origin::Manual,integer(7)));
        CHECK(host.History().Records().size()==retainedSize);
        inputSequence=0;idle();
        // Branch B activates a real spell instead of A's end-turn. No replayed
        // expected decision is injected into either independent AI process.
        submit(integer(5),Origin::Manual);idle();
        CHECK(host.History().Records().at(*targetIndex).response==integer(5));
        submit(integer(7),Origin::Manual);idle(); // actual candidate AI continues an entire new turn
        const auto abortHistory=host.History().Records().size();const auto abortPid=host.BotStatus()->identity.activePid;
        const auto abortPrompt=host.CurrentBoundary().checkpoint.prompt;
        host.ReceiveUndo(&human,{WireKind::Request,{session,1,host.Status().nextRequest,0,{}},{}});
        const auto abortKey=host.ActiveKey();
        pump(host,[&]{return host.BotStatus()->identity.state==BotState::Ready;});
        FragmentAssembler abortFragments;
        for(const auto& e:output)if(e.kind==WireKind::Prepare && SameKey(e.key,abortKey))abortFragments.Add(e.payload);
        auto invalid=DecodePlayerRestore(DecodeRoomRestore(abortFragments.Finish()).visible);invalid.visibleDigest[0]^=1;
        CHECK(!client.Prepare(abortKey,invalid,invalid.prompt));
        host.ReceiveUndo(&human,{WireKind::Ready,abortKey,{0}});client.Abort(abortKey);
        host.ReceiveUndo(&human,{WireKind::AbortAck,abortKey,{}});
        pump(host,[&]{return host.Status().state==TxState::Running;});
        CHECK(host.InstalledEpoch()==1 && host.History().Records().size()==abortHistory);
        CHECK(host.BotStatus()->identity.activePid==abortPid && !host.BotStatus()->candidatePid && !host.BotStatus()->retainedPid);
        CHECK(host.CurrentBoundary().checkpoint.prompt==abortPrompt);alive(abortPid);
        submit(integer(5),Origin::Manual);idle(); // old AI/core continue after actual failed N3 prepare
        std::cout<<"PASS actual N3 preparation failure aborts private candidate and resumes original AI/core branch"<<std::endl;
        std::cout<<"PASS actual host "<<executor<<" Join/deck/Ready/RPS/TP/AI decisions/N3+AI prepare/commit/resume branch B"<<std::endl;
        }
        }
    }
    CHECK(measurement::CountDescendants()==baseline);
}
int main(int argc,char** argv) {try {
    CHECK(argc==3);WSADATA winsock;CHECK(WSAStartup(MAKEWORD(2,2),&winsock)==0);
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
    auto resources=ResourceView::Capture(argv[2],dataManager,false);
    const auto executable=std::filesystem::absolute(std::filesystem::u8path(argv[1])).wstring();
    const auto runtime=(std::filesystem::u8path(argv[2])/"WindBot").u8string();
    for(const auto* executor:{"ChainBurn","Dragun"})branch(resources,executable,runtime,executor);
    branch(resources,executable,runtime,"ChainBurn","missing-ack");
    branch(resources,executable,runtime,"ChainBurn","broken-pipe");
    WSACleanup();return 0;
} catch(const std::exception& e) {std::cerr<<"FAIL undo_bot_host_tests: "<<e.what()<<std::endl;return 1;} }
