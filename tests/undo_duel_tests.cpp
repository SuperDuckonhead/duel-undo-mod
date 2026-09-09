#include "test_support.h"
#include "undo_duel.h"
#include "data_manager.h"
#include "game.h"
#include "undo/room_restore.h"
#include "undo/player_restore.h"
#include "undo/rebuilder.h"
#include <chrono>
#include <thread>
#include <filesystem>
#include <iostream>
namespace ygo {
bool ClientField::OnEvent(const irr::SEvent&) {throw std::runtime_error("Unexpected GUI event in host test");}
void Game::AddDebugMsg(const char*) {throw std::runtime_error("Unexpected GUI host test sink");}
void DeckBuilder::RefreshPackListScroll() {throw std::runtime_error("Unexpected editor host test sink");}
}
namespace irr { namespace io { IFileSystem* createFileSystem(); } }
using namespace ygo;
using namespace undo;
static Bytes integer(std::uint32_t n) {return {std::uint8_t(n),std::uint8_t(n>>8),std::uint8_t(n>>16),std::uint8_t(n>>24)};}
int main(int argc,char** argv) {try {
    CHECK(argc==2 || argc==3);const std::string mode=argc==3?argv[2]:"";
    std::unique_ptr<irr::io::IFileSystem,void(*)(irr::io::IFileSystem*)> files(irr::io::createFileSystem(),[](auto* p){p->drop();});
    dataManager.IrrFileSystem=files.get();
    CHECK(dataManager.LoadDB((std::filesystem::u8path(argv[1])/"cards.cdb").u8string().c_str()));
    auto resources=ResourceView::Capture(argv[1]);
    for(bool noCheck:{false,true}) for(bool noShuffle:{false,true}) {
        auto config=std::make_shared<RoomConfig>();config->resources=resources;
        config->capability.resources=resources->Fingerprint();config->capability.mode=noCheck?RoomMode::ConsentLan:RoomMode::LoopbackFree;
        auto session=NewSessionId();
        std::array<std::vector<Envelope>,2> output;
        GamePacketStream clockStream(session,0);
        std::vector<ClockState> turnClocks;
        UndoDuel duel(false,config,session,[&](DuelPlayer* p,const Envelope& e){
            output.at(p->endpointId-1).push_back(e);
            if(mode=="turn-clock" && p->endpointId==1 && e.kind==WireKind::Game) {
                auto packet=clockStream.Add(e);
                if(packet && packet->packet.size()>1 && packet->packet[0]==STOC_GAME_MSG && packet->packet[1]==MSG_NEW_TURN)
                    turnClocks.push_back(duel.Status().clock);
            }
            return true;
        });
        DuelPlayer a{},b{};a.endpointId=1;b.endpointId=2;a.undoPeer.ready=b.undoPeer.ready=true;
        BufferIO::CopyCharArray(L"Host",a.name);BufferIO::CopyCharArray(L"Friend",b.name);
        duel.host_info.duel_rule=5;duel.host_info.start_lp=8000;duel.host_info.start_hand=5;
        duel.host_info.time_limit=mode.rfind("deadline",0)==0?1:60;duel.host_info.draw_count=1;duel.host_info.no_check_deck=noCheck;duel.host_info.no_shuffle_deck=noShuffle;
        duel.JoinGame(&a,nullptr,true);
        CTOS_JoinGame join{};join.version=PRO_VERSION;duel.JoinGame(&b,reinterpret_cast<unsigned char*>(&join),false);
        Bytes deck;BufferIO::VectorWrite<std::uint32_t>(deck,40);BufferIO::VectorWrite<std::uint32_t>(deck,0);
        for(int i=0;i<40;++i)BufferIO::VectorWrite<std::uint32_t>(deck,i%2?46986414:89631139);
        duel.UpdateDeck(&a,deck.data(),deck.size());duel.UpdateDeck(&b,deck.data(),deck.size());
        a.state=CTOS_TP_RESULT;duel.TPResult(&a,noShuffle?1:0);
        CHECK(duel.HasActiveDuel());CHECK(duel.Initial().noCheckDeck==noCheck && duel.Initial().noShuffleDeck==noShuffle);
        std::array<std::uint64_t,2> inputSequence{};
        auto submit=[&](Origin origin,Bytes response) {
            auto now=duel.CurrentBoundary();CHECK(now.kind==BoundaryKind::AwaitResponse);
            auto* p=now.checkpoint.player==a.type?&a:&b;
            auto s=duel.Status();
            const auto slot=p==&a?0:1;
            for(const auto& envelope:EncodeGamePacket(session,duel.InstalledEpoch(),s.prompt,++inputSequence[slot],{CTOS_TIME_CONFIRM}))duel.ReceiveUndo(p,envelope);
            duel.ReceiveUndo(p,EncodeResponse({session,duel.InstalledEpoch(),s.prompt,0,{}},origin,response));
        };
        auto idle=[&] {
            for(int i=0;duel.CurrentBoundary().checkpoint.prompt.at(0)!=MSG_SELECT_IDLECMD;++i) {
                CHECK(i<40);CHECK(duel.CurrentBoundary().checkpoint.prompt[0]==MSG_SELECT_CHAIN);
                submit(Origin::Automatic,integer(0xffffffffu));
            }
        };
        idle();auto original=duel.CurrentBoundary().checkpoint;
        CHECK(original.player==0);CHECK(a.type==(noShuffle?0:1));
        auto retryPrompt=duel.Status().prompt;auto retryCount=duel.History().Records().size();
        submit(Origin::Manual,integer(0xfffffff0u));
        CHECK(duel.Status().prompt==retryPrompt && duel.History().Records().size()==retryCount);
        CHECK(SamePosition(duel.CurrentBoundary().checkpoint,original));
        if(mode=="deadline-response") {
            const auto count=duel.History().Records().size();
            std::this_thread::sleep_for(std::chrono::milliseconds(1100));
            submit(Origin::Manual,integer(7));
            CHECK(duel.History().Records().size()==count && !duel.HasActiveDuel());
            CHECK(duel.ActiveKey().session==SessionId{});
            std::cout<<"expired turn-ending response rejected before timer callback"<<std::endl;return 0;
        }
        if(mode=="turn-clock")std::this_thread::sleep_for(std::chrono::milliseconds(25));
        submit(Origin::Manual,integer(7));idle();
        if(mode=="turn-clock") {
            CHECK(turnClocks.size()==2);
            // Observe the reset at the actual outbound NEW_TURN. Automatic
            // chain answers between that event and idle legitimately spend time.
            CHECK(turnClocks.back().remainingMs[0]==60000 && turnClocks.back().remainingMs[1]==60000);
            std::cout<<"real NEW_TURN replenishes authoritative allowance"<<std::endl;return 0;
        }
        submit(Origin::Manual,integer(7));idle();
        if(mode=="deadline-request") {
            const auto count=duel.History().Records().size();const auto keyBefore=duel.ActiveKey();
            auto* expired=duel.CurrentBoundary().checkpoint.player==a.type?&a:&b;
            CHECK(duel.History().Target(expired->type).has_value());
            std::this_thread::sleep_for(std::chrono::milliseconds(1100));
            TxKey request{session,duel.InstalledEpoch(),duel.Status().nextRequest,0,{}};
            duel.ReceiveUndo(expired,{WireKind::Request,request,{}});
            CHECK(duel.History().Records().size()==count && !duel.HasActiveDuel());
            CHECK(SameKey(duel.ActiveKey(),keyBefore));
            std::cout<<"expired undo request rejected before timer callback"<<std::endl;return 0;
        }
        CHECK(duel.History().Target(a.type).has_value());
        auto keep=*duel.History().Target(a.type);auto target=duel.History().Records().at(keep).before;
        auto status=duel.Status();TxKey request{session,duel.InstalledEpoch(),status.nextRequest,0,{}};
        duel.ReceiveUndo(&a,{WireKind::Request,request,{}});
        auto key=duel.ActiveKey();
        if(noCheck) {
            CHECK(duel.Status().state==TxState::Consent);
            auto frozenClock=duel.Status().clock;
            std::this_thread::sleep_for(std::chrono::milliseconds(25));duel.PollUndo();
            CHECK(duel.Status().clock.remainingMs==frozenClock.remainingMs);
            duel.ReceiveUndo(&a,{WireKind::Consent,key,{1}});CHECK(duel.Status().state==TxState::Consent);
            duel.ReceiveUndo(&b,{WireKind::Consent,key,{1}});
        }
        CHECK(duel.Status().state==TxState::Preparing);
        auto payloads=[&](int p) {
            std::vector<Bytes> result;
            for(const auto& e:output[p])if(e.kind==WireKind::Prepare && SameKey(e.key,key))result.push_back(e.payload);
            return result;
        };
        std::array<std::vector<Bytes>,2> initialFragments{payloads(0),payloads(1)};
        if(mode=="duplicate") {
            duel.ReceiveUndo(&a,{WireKind::Request,request,{}});
            for(int p=0;p<2;++p) {
                auto got=payloads(p);CHECK(got.size()==2*initialFragments[p].size());
                CHECK(std::equal(initialFragments[p].begin(),initialFragments[p].end(),got.begin()+initialFragments[p].size()));
            }
        }
        std::array<std::unique_ptr<FragmentAssembler>,2> fragments;
        for(int p=0;p<2;++p) {
            fragments[p]=std::make_unique<FragmentAssembler>();
            for(const auto& bytes:initialFragments[p])fragments[p]->Add(bytes);
            auto descriptor=DecodeRoomRestore(fragments[p]->Finish());
            auto visible=DecodePlayerRestore(descriptor.visible);
            CHECK(visible.player==(p==0?a.type:b.type));
            CHECK(visible.prompt==(visible.player==target.player?target.prompt:Bytes{MSG_WAITING}));
            duel.ReceiveUndo(p==0?&a:&b,{WireKind::Ready,key,{1}});
            if(mode=="duplicate" && p==0) {
                duel.ReceiveUndo(&a,{WireKind::Request,request,{}});
                for(int slot=0;slot<2;++slot) {
                    auto got=payloads(slot);CHECK(got.size()==3*initialFragments[slot].size());
                    CHECK(std::equal(initialFragments[slot].begin(),initialFragments[slot].end(),got.begin()+2*initialFragments[slot].size()));
                }
            }
        }
        for(int i=0;i<5000 && duel.Status().state==TxState::Preparing;++i) {
            duel.PollUndo();std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        CHECK(duel.Status().state==TxState::Committing);
        CHECK(duel.InstalledEpoch()==1);
        CHECK(SamePosition(duel.CurrentBoundary().checkpoint,target));
        CHECK(duel.History().Records().size()==keep);
        Bytes epoch(8);epoch[0]=1;
        duel.ReceiveUndo(&a,{WireKind::CommitAck,key,epoch});
        CHECK(duel.Status().state==TxState::Committing);
        duel.ReceiveUndo(&b,{WireKind::CommitAck,key,epoch});
        CHECK(duel.Status().state==TxState::Running);
        CHECK(duel.Status().clock.remainingMs==target.clock.remainingMs);
        inputSequence={};
        auto count=duel.History().Records().size();
        duel.ReceiveUndo(&a,EncodeResponse({session,0,duel.Status().prompt,0,{}},Origin::Manual,integer(7)));
        CHECK(duel.History().Records().size()==count);
        submit(Origin::Manual,integer(7));idle();
        CHECK(duel.History().Records().size()>count);
        auto current=duel.CurrentBoundary().checkpoint;auto beforeAbort=duel.History().Records().size();
        // A repeated completed request can replay Resume, but cannot restore an old clock or branch.
        auto beforeDuplicate=duel.Status().clock;
        duel.ReceiveUndo(&a,{WireKind::Request,request,{}});
        CHECK(SamePosition(duel.CurrentBoundary().checkpoint,current));
        CHECK(duel.Status().clock.remainingMs[0]<=beforeDuplicate.remainingMs[0] && duel.Status().clock.remainingMs[1]<=beforeDuplicate.remainingMs[1]);
        auto secondStatus=duel.Status();TxKey second{session,duel.InstalledEpoch(),secondStatus.nextRequest,0,{}};
        duel.ReceiveUndo(&a,{WireKind::Request,second,{}});auto failedKey=duel.ActiveKey();
        if(noCheck)duel.ReceiveUndo(&b,{WireKind::Consent,failedKey,{1}});
        CHECK(duel.Status().state==TxState::Preparing);
        duel.ReceiveUndo(&b,{WireKind::Ready,failedKey,{0}});
        CHECK(duel.Status().state==TxState::Aborting);
        duel.ReceiveUndo(&a,{WireKind::AbortAck,failedKey,{}});
        duel.ReceiveUndo(&a,{WireKind::AbortAck,failedKey,{}});
        CHECK(duel.Status().state==TxState::Aborting);
        duel.ReceiveUndo(&b,{WireKind::AbortAck,failedKey,{}});
        CHECK(duel.Status().state==TxState::Running);
        CHECK(duel.History().Records().size()==beforeAbort);
        CHECK(SamePosition(duel.CurrentBoundary().checkpoint,current));
        // The original branch still accepts the next actual legal response.
        submit(Origin::Manual,integer(7));idle();
        CHECK(duel.History().Records().size()>beforeAbort);
    }
    std::cout<<"real host core/options/recipient restore, two Ready/Ack barrier and new branch passed\n";
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
