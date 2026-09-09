#include "test_support.h"
#include "netserver.h"
#include "data_manager.h"
#include "game.h"
#include "mysocket.h"
#include "undo/room_wire.h"
#include "undo/room_restore.h"
#include "undo/player_restore.h"
#include <chrono>
#include <thread>
#include <filesystem>
#include <map>
#include <iostream>
namespace irr { namespace io { IFileSystem* createFileSystem(); } }
namespace ygo {
bool ClientField::OnEvent(const irr::SEvent&) {throw std::runtime_error("GUI event in TCP host test");}
void Game::AddDebugMsg(const char*) {throw std::runtime_error("GUI diagnostics in TCP host test");}
void DeckBuilder::RefreshPackListScroll() {throw std::runtime_error("Editor pack refresh in TCP host test");}
}
using namespace ygo;
using namespace undo;
#include "tcp_peer.h"
static Bytes integer(std::uint32_t n) {return {std::uint8_t(n),std::uint8_t(n>>8),std::uint8_t(n>>16),std::uint8_t(n>>24)};}
static void stopped() {
    for(int n=0;n<2000 && NetServer::IsRunning();++n)std::this_thread::sleep_for(std::chrono::milliseconds(2));
    CHECK(!NetServer::IsRunning());
}
struct Client {
    Peer peer;
    ClientRoomHandshake handshake;
    std::unique_ptr<GamePacketStream> stream;
    std::uint64_t epoch{}, inputSequence{};
    std::uint8_t player{2};
    bool ended{}, winSeen{};
    Bytes replayBytes;
    RoomStatus status;
    std::vector<Bytes> journal;
    Bytes lastPrompt;
    std::map<std::uint64_t,Digest> visibleAt;
    Client(unsigned short port,const Hello& capability):peer(port),handshake(capability,true) {}
    void hello() {
        auto reply=handshake.Receive(peer.NextHello());CHECK(reply);
        std::this_thread::sleep_for(std::chrono::milliseconds(60)); // Real UI work may delay challenge echo.
        peer.Envelope(*reply);
        handshake.Receive(peer.NextHello());CHECK(handshake.Ready());
        stream=std::make_unique<GamePacketStream>(handshake.Session(),0);
    }
    Envelope receive() {
        for(int n=0;n<1000;++n) {
            auto packet=peer.Read();CHECK(!packet.empty());
            if(packet[0]!=RoomOuterOpcode)continue;
            auto e=Decode(Bytes(packet.begin()+1,packet.end()));
            if(e.kind==WireKind::Game) {
                auto p=stream->Add(e);if(!p)continue;
                if(p->packet[0]==STOC_DUEL_END)ended=true;
                if(p->packet[0]==STOC_REPLAY) {
                    CHECK(winSeen); // No full replay/private deck data before the terminal result.
                    CHECK(replayBytes.empty());replayBytes.assign(p->packet.begin()+1,p->packet.end());
                }
                if(p->packet[0]==STOC_GAME_MSG) {
                    Bytes body(p->packet.begin()+1,p->packet.end());
                    if(body[0]==MSG_START)player=body.at(1);
                    if(body[0]==MSG_WIN)winSeen=true;
                    if(body[0]!=MSG_RETRY && body[0]!=MSG_WIN)journal.push_back(body);
                    lastPrompt=body;
                }
            } else if(e.kind==WireKind::Status) {
                if(e.key.session!=handshake.Session() || e.key.epoch!=epoch)continue;
                status=DecodeRoomStatus(e.payload);
                if(status.state==TxState::Running && status.prompt && player<2 &&
                   !visibleAt.count(status.prompt) && !journal.empty()) {
                    auto prefix=journal;auto prompt=prefix.back();prefix.pop_back();
                    visibleAt.emplace(status.prompt,BuildPlayerRestore(player,prefix,prompt).visibleDigest);
                }
            }
            return e;
        }
        throw std::runtime_error("No extension packet");
    }
    void boundaryAfter(std::uint64_t old) {
        for(int n=0;n<2000;++n) {
            auto e=receive();
            if(ended)return;
            if(e.kind==WireKind::Status && status.state==TxState::Running && status.prompt>old)return;
        }
        throw std::runtime_error("No next TCP prompt boundary");
    }
    void rawUntil(std::uint8_t opcode) {
        for(int n=0;n<100;++n){auto packet=peer.Read();CHECK(!packet.empty());if(packet[0]==opcode)return;}
        throw std::runtime_error("Missing native lobby message");
    }
    Envelope kind(WireKind wanted,const TxKey* key=nullptr) {
        for(int n=0;n<4000;++n) {auto e=receive();if(e.kind==wanted && (!key || SameKey(e.key,*key)))return e;}
        throw std::runtime_error("Missing transaction control");
    }
    void respond(const Bytes& bytes,Origin origin=Origin::Manual) {
        const auto session=handshake.Session();
        for(const auto& e:EncodeGamePacket(session,epoch,status.prompt,++inputSequence,{CTOS_TIME_CONFIRM}))peer.Envelope(e);
        peer.Envelope(EncodeResponse({session,epoch,status.prompt,0,{}},origin,bytes));
    }
};
int main(int argc,char** argv) {try {
    CHECK(argc==2 || argc==3);const std::string fault=argc==3?argv[2]:"";WSADATA winsock;CHECK(WSAStartup(MAKEWORD(2,2),&winsock)==0);
    std::unique_ptr<irr::io::IFileSystem,void(*)(irr::io::IFileSystem*)> files(irr::io::createFileSystem(),[](auto* p){p->drop();});
    dataManager.IrrFileSystem=files.get();
    CHECK(dataManager.LoadDB((std::filesystem::u8path(argv[1])/"cards.cdb").u8string().c_str()));
    auto config=std::make_shared<RoomConfig>();config->resources=ResourceView::Capture(argv[1]);
    config->capability.engine[0]=1;config->capability.rules[0]=2;
    config->capability.resources=config->resources->Fingerprint();config->capability.mode=RoomMode::LoopbackFree;
    unsigned short port{};CHECK(NetServer::StartServer(0,0x7f000001,&port,false,&config->capability,config));
    Client a(port,config->capability);a.peer.Envelope(a.handshake.Offer());
    CTOS_PlayerInfo name{};BufferIO::CopyCharArray(L"TCP Host",name.name);a.peer.SendStruct(CTOS_PLAYER_INFO,name);
    CTOS_CreateGame create{};create.info.duel_rule=5;create.info.start_lp=8000;create.info.start_hand=fault=="replay"?1:5;
    create.info.draw_count=1;create.info.no_check_deck=1;create.info.no_shuffle_deck=1;create.info.time_limit=60;
    a.peer.SendStruct(CTOS_CREATE_GAME,create);a.hello();
    if(fault=="replacement") {
        Client previous(port,config->capability);previous.peer.Envelope(previous.handshake.Offer());
        BufferIO::CopyCharArray(L"Leaving friend",name.name);previous.peer.SendStruct(CTOS_PLAYER_INFO,name);
        CTOS_JoinGame request{};request.version=PRO_VERSION;previous.peer.SendStruct(CTOS_JOIN_GAME,request);previous.hello();
        previous.peer.Close();
        bool left=false;
        for(int n=0;n<100 && !left;++n) {
            auto packet=a.peer.Read();CHECK(!packet.empty());
            left=packet[0]==STOC_HS_PLAYER_CHANGE && packet.size()==2 && packet[1]==((1<<4)|PLAYERCHANGE_LEAVE);
        }
        CHECK(left);
    }
    Client b(port,config->capability);b.peer.Envelope(b.handshake.Offer());
    BufferIO::CopyCharArray(L"TCP Friend",name.name);b.peer.SendStruct(CTOS_PLAYER_INFO,name);
    CTOS_JoinGame join{};join.version=PRO_VERSION;b.peer.SendStruct(CTOS_JOIN_GAME,join);b.hello();
    const auto deck=[&](std::uint32_t code) {
        const auto count=fault=="replay"?6:40;
        Bytes bytes;BufferIO::VectorWrite<std::uint32_t>(bytes,count);BufferIO::VectorWrite<std::uint32_t>(bytes,0);
        for(int i=0;i<count;++i)BufferIO::VectorWrite<std::uint32_t>(bytes,code);
        return bytes;
    };
    a.peer.Send(CTOS_UPDATE_DECK,deck(46986414));b.peer.Send(CTOS_UPDATE_DECK,deck(89631139));
    a.peer.Send(CTOS_HS_READY);b.peer.Send(CTOS_HS_READY);
    // Wait for both real ready broadcasts before issuing the host start action.
    bool bothReady=false;unsigned readyMask{};
    for(int n=0;n<100 && !bothReady;++n) {
        auto packet=a.peer.Read();CHECK(!packet.empty());
        if(packet[0]==STOC_HS_PLAYER_CHANGE && packet.size()==2 && (packet[1]&15)==PLAYERCHANGE_READY)
            readyMask|=1u<<(packet[1]>>4);
        bothReady=readyMask==3;
    }
    CHECK(bothReady);a.peer.Send(CTOS_HS_START);
    a.rawUntil(STOC_SELECT_HAND);b.rawUntil(STOC_SELECT_HAND);
    CTOS_HandResult hand{};hand.res=1;a.peer.SendStruct(CTOS_HAND_RESULT,hand);
    hand.res=3;b.peer.SendStruct(CTOS_HAND_RESULT,hand);
    a.rawUntil(STOC_SELECT_TP);CTOS_TPResult first{};first.res=1;a.peer.SendStruct(CTOS_TP_RESULT,first);
    a.boundaryAfter(0);b.boundaryAfter(0);
    auto disconnectFriend=[&] {
        b.peer.Close();
        for(int n=0;n<2000 && !a.ended;++n)a.receive();
        CHECK(a.ended);a.peer.Close();stopped();WSACleanup();
    };
    std::vector<Bytes> acceptedInputs;
    auto advance=[&](Bytes response,Origin origin) {
        auto old=a.status.prompt;CHECK(old==b.status.prompt);
        auto& actor=a.status.promptPlayer==a.player?a:b;actor.respond(response,origin);
        a.boundaryAfter(old);b.boundaryAfter(old);acceptedInputs.push_back(response);
    };
    auto idle=[&] {
        if(a.ended)return;
        for(int i=0;i<40;++i) {
            if(a.ended)return;
            auto& actor=a.status.promptPlayer==a.player?a:b;
            if(actor.lastPrompt.at(0)==MSG_SELECT_IDLECMD)return;
            if(actor.lastPrompt.at(0)!=MSG_SELECT_CHAIN)std::cerr<<"Unexpected prompt="<<unsigned(actor.lastPrompt.at(0))<<" size="<<actor.lastPrompt.size()<<" player="<<unsigned(actor.player)<<" hoststatus="<<unsigned(a.status.promptPlayer)<<" prompt="<<a.status.prompt<<std::endl;
            CHECK(actor.lastPrompt.at(0)==MSG_SELECT_CHAIN);
            advance(integer(0xffffffffu),Origin::Automatic);
        }
        throw std::runtime_error("Did not reach actual idle prompt");
    };
    idle();const auto targetPrompt=a.status.prompt;CHECK(a.status.promptPlayer==a.player);
    const auto retainedCount=acceptedInputs.size();
    advance(integer(7),Origin::Manual);idle();advance(integer(7),Origin::Manual);idle();
    for(auto* client:{&a,&b}) {
        const auto hidden=integer(client==&a?89631139:46986414);
        for(const auto& frame:client->journal)
            CHECK(std::search(frame.begin(),frame.end(),hidden.begin(),hidden.end())==frame.end());
    }
    TxKey request{a.handshake.Session(),0,a.status.nextRequest,0,{}};
    a.peer.Envelope({WireKind::Request,request,{}});
    std::array<std::unique_ptr<ClientRestore>,2> restores;
    ClientField fields[2];PlayerViewState states[2];TxKey key;
    int index=0;
    for(auto* client:{&a,&b}) {
        auto firstFragment=client->kind(WireKind::Prepare);if(index==0)key=firstFragment.key;else CHECK(SameKey(key,firstFragment.key));
        FragmentAssembler fragments;fragments.Add(firstFragment.payload);
        const auto count=std::uint32_t(firstFragment.payload[4])|(std::uint32_t(firstFragment.payload[5])<<8)|
            (std::uint32_t(firstFragment.payload[6])<<16)|(std::uint32_t(firstFragment.payload[7])<<24);
        for(std::uint32_t i=1;i<count;++i)fragments.Add(client->kind(WireKind::Prepare,&key).payload);
        auto descriptor=DecodeRoomRestore(fragments.Finish());CHECK(descriptor.prompt==targetPrompt);
        auto visible=DecodePlayerRestore(descriptor.visible);CHECK(visible.player==client->player);
        CHECK(visible.visibleDigest==client->visibleAt.at(targetPrompt));
        restores[index]=std::make_unique<ClientRestore>(fields[index],states[index],client->player,key.session,0);
        CHECK(restores[index]->Prepare(key,visible,visible.prompt));
        if(fault=="prepare" && index==1) {disconnectFriend();std::cout<<"actual guest EOF during prepare cleaned up safely"<<std::endl;return 0;}
        if(fault=="policy" && index==1) {
            // Both candidates exist and the second Ready is withheld. Policy
            // refusal must be out-of-band even while gameplay is frozen.
            for(int attempt=0;attempt<4;++attempt) {
                a.peer.Send(CTOS_HS_TOOBSERVER);
                bool received=false;
                for(int n=0;n<100 && !received;++n) {
                    auto packet=a.peer.Read();CHECK(!packet.empty());
                    if(packet[0]!=STOC_ERROR_MSG)continue;
                    CHECK(packet.size()==1+sizeof(STOC_ErrorMsg));
                    STOC_ErrorMsg error{};std::memcpy(&error,packet.data()+1,sizeof error);
                    CHECK(error.msg==0x7e && error.code==2);received=true;
                }
                CHECK(received);
            }
        }
        if(fault=="busy" && index==1) {
            auto competing=request; // Both UI instances saw the same advertised request ID.
            b.peer.Envelope({WireKind::Request,competing,{}});
            auto rejected=b.kind(WireKind::RequestRejected,&competing);
            CHECK(rejected.payload==Bytes{1});
            CHECK(b.status.state==TxState::Preparing && SameKey(firstFragment.key,key));
            // The selected transaction is still the original one; the same
            // candidates can complete after this explicit, directed refusal.
        }
        client->peer.Envelope({WireKind::Ready,key,{1}});++index;
    }
    index=0;
    for(auto* client:{&a,&b}) {
        auto commit=client->kind(WireKind::Commit,&key);CHECK(commit.payload.size()==8 && commit.payload[0]==1);
        CHECK(restores[index]->Commit(key,1));client->epoch=1;client->inputSequence=0;client->stream->Reset(key.session,1);
        if(fault=="commit" && index==0) {disconnectFriend();std::cout<<"actual guest EOF after commit cleaned up safely"<<std::endl;return 0;}
        client->peer.Envelope({WireKind::CommitAck,key,commit.payload});++index;
    }
    index=0;
    for(auto* client:{&a,&b}) {
        client->kind(WireKind::Resume,&key);CHECK(restores[index]->Resume(key));
        for(int n=0;n<1000;++n) {auto e=client->receive();if(e.kind==WireKind::Status && client->status.state==TxState::Running)break;}
        CHECK(client->status.prompt==targetPrompt);++index;
    }
    // Current epoch proceeds through the real socket/core/filter path after restore.
    acceptedInputs.resize(retainedCount);
    auto before=a.status.prompt;a.respond(integer(7));acceptedInputs.push_back(integer(7));
    a.boundaryAfter(before);b.boundaryAfter(before);idle();
    if(fault=="replay") {
        for(int turn=0;turn<16 && !a.ended;++turn){advance(integer(7),Origin::Manual);idle();}
        CHECK(a.ended && b.ended && a.winSeen && b.winSeen);
        CHECK(!a.replayBytes.empty() && a.replayBytes==b.replayBytes);
        Replay replay;CHECK(replay.LoadUndoReplay(a.replayBytes));
        auto core=replay.CreateUndoDriver(config->resources);auto boundary=core->Advance();
        for(const auto& expected:acceptedInputs) {
            CHECK(boundary.kind==BoundaryKind::AwaitResponse);
            Bytes recorded;CHECK(replay.ReadUndoResponse(boundary.checkpoint,recorded));CHECK(recorded==expected);
            core->Submit(recorded);boundary=core->Advance();
        }
        CHECK(boundary.kind==BoundaryKind::Finished);
        Bytes extra;CHECK(!replay.ReadUndoResponse(boundary.checkpoint,extra));
        a.peer.Close();b.peer.Close();stopped();WSACleanup();
        std::cout<<"actual TCP post-duel retained replay arrives once, both copies equal and replay to terminal with exact retained inputs"<<std::endl;
        return 0;
    }
    disconnectFriend();
    std::cout<<"actual TCP lobby/ready/RPS/core, hidden-card filtering, two real N3 models, commit/Resume and continued play passed\n";
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
