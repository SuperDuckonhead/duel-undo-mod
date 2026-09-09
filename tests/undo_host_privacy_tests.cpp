#include "test_support.h"
#include "undo_duel.h"
#include "data_manager.h"
#include "game.h"
#include "undo/room_restore.h"
#include "undo/player_restore.h"
#include <filesystem>
#include <iostream>
#include <memory>

namespace ygo {
bool ClientField::OnEvent(const irr::SEvent&) { throw std::runtime_error("Unexpected GUI event in privacy test"); }
void Game::AddDebugMsg(const char*) { throw std::runtime_error("Unexpected GUI log in privacy test"); }
void DeckBuilder::RefreshPackListScroll() { throw std::runtime_error("Unexpected editor in privacy test"); }
}
namespace irr { namespace io { IFileSystem* createFileSystem(); } }
using namespace ygo;
using namespace undo;
namespace {
constexpr std::uint32_t Upstart = 70368879, Mst = 5318639;
Bytes integer(std::uint32_t value) {
    return {std::uint8_t(value),std::uint8_t(value>>8),std::uint8_t(value>>16),std::uint8_t(value>>24)};
}
std::uint32_t word(const Bytes& bytes,std::size_t at) {
    return std::uint32_t(bytes.at(at)) | std::uint32_t(bytes.at(at+1))<<8 |
        std::uint32_t(bytes.at(at+2))<<16 | std::uint32_t(bytes.at(at+3))<<24;
}
std::uint32_t idleCommand(const Bytes& prompt,unsigned action,std::uint32_t code) {
    CHECK(prompt.at(0)==MSG_SELECT_IDLECMD && action<=5);
    std::size_t at=2;
    for(unsigned group=0;group<=5;++group) {
        const auto count=prompt.at(at++);
        const auto width=group==5?11:7;
        for(unsigned i=0;i<count;++i,at+=width) {
            if(group==action && word(prompt,at)==code)return (i<<16)|action;
        }
    }
    throw std::runtime_error("Required real idle card action absent");
}
Bytes place(const Bytes& prompt) {
    // count zero is the actual cancelable single-place form; choose a real slot.
    CHECK(prompt.at(0)==MSG_SELECT_PLACE && prompt.at(2)<=1);
    const auto disabled=word(prompt,3);
    for(unsigned slot=0;slot<5;++slot)
        if(!(disabled & (1u<<(8+slot))))return {prompt.at(1),LOCATION_SZONE,std::uint8_t(slot)};
    throw std::runtime_error("No available real spell placement");
}
Bytes deck(std::uint32_t code) {
    Bytes bytes;BufferIO::VectorWrite<std::uint32_t>(bytes,40);BufferIO::VectorWrite<std::uint32_t>(bytes,0);
    for(int i=0;i<40;++i)BufferIO::VectorWrite(bytes,code);
    return bytes;
}
struct Observed {
    std::uint64_t prompt;
    Bytes frame;
};
}
int main(int argc,char** argv) { try {
    CHECK(argc==2);
    std::unique_ptr<irr::io::IFileSystem,void(*)(irr::io::IFileSystem*)> filesystem(
        irr::io::createFileSystem(),[](auto* p){p->drop();});
    dataManager.IrrFileSystem=filesystem.get();
    CHECK(dataManager.LoadDB((std::filesystem::u8path(argv[1])/"cards.cdb").u8string().c_str()));
    CardData data{};
    CHECK(dataManager.GetData(Upstart,&data) && (data.type&TYPE_SPELL));
    CHECK(dataManager.GetData(Mst,&data) && (data.type&TYPE_SPELL) && (data.type&TYPE_QUICKPLAY));
    auto resources=ResourceView::Capture(argv[1]);
    bool everyRecipientMatched=true;
    for(bool swapped:{false,true}) {
        auto config=std::make_shared<RoomConfig>();config->resources=resources;
        config->capability.resources=resources->Fingerprint();config->capability.mode=RoomMode::LoopbackFree;
        const auto session=NewSessionId();
        std::array<std::vector<Envelope>,2> envelopes;
        std::array<std::vector<Observed>,2> visible;
        std::array<std::unique_ptr<GamePacketStream>,2> streams;
        for(auto& stream:streams)stream=std::make_unique<GamePacketStream>(session,0);
        DuelPlayer host{},peer{};
        host.endpointId=1;peer.endpointId=2;host.undoPeer.ready=peer.undoPeer.ready=true;
        UndoDuel duel(false,config,session,[&](DuelPlayer* recipient,const Envelope& envelope) {
            const auto slot=recipient->endpointId-1;CHECK(slot<2);
            envelopes[slot].push_back(envelope);
            if(envelope.kind==WireKind::Game) {
                auto packet=streams[slot]->Add(envelope);
                if(packet && packet->packet.at(0)==STOC_GAME_MSG)
                    visible[slot].push_back({packet->prompt,Bytes(packet->packet.begin()+1,packet->packet.end())});
            }
            return true;
        });
        BufferIO::CopyCharArray(L"Host",host.name);BufferIO::CopyCharArray(L"Peer",peer.name);
        duel.host_info.duel_rule=5;duel.host_info.start_lp=8000;duel.host_info.start_hand=5;
        duel.host_info.time_limit=0;duel.host_info.draw_count=1;
        duel.host_info.no_check_deck=true;duel.host_info.no_shuffle_deck=true;
        duel.JoinGame(&host,nullptr,true);
        CTOS_JoinGame join{};join.version=PRO_VERSION;
        duel.JoinGame(&peer,reinterpret_cast<unsigned char*>(&join),false);
        auto firstDeck=deck(Upstart),secondDeck=deck(Mst);
        auto* first=swapped?&peer:&host;auto* second=swapped?&host:&peer;
        duel.UpdateDeck(first,firstDeck.data(),firstDeck.size());
        duel.UpdateDeck(second,secondDeck.data(),secondDeck.size());
        host.state=CTOS_TP_RESULT;duel.TPResult(&host,swapped?0:1);
        CHECK(duel.HasActiveDuel() && first->type==0 && second->type==1);
        auto submit=[&](Origin origin,const Bytes& response) {
            const auto before=duel.CurrentBoundary();CHECK(before.kind==BoundaryKind::AwaitResponse);
            auto* actor=before.checkpoint.player==first->type?first:second;
            duel.ReceiveUndo(actor,EncodeResponse({session,duel.InstalledEpoch(),duel.Status().prompt,0,{}},origin,response));
            CHECK(duel.Status().state==TxState::Running);
            CHECK(!duel.CurrentBoundary().rejectedResponse);
        };
        auto reach=[&](std::uint8_t message) {
            for(unsigned step=0;duel.CurrentBoundary().checkpoint.prompt.at(0)!=message;++step) {
                CHECK(step<50);
                const auto prompt=duel.CurrentBoundary().checkpoint.prompt;
                if(prompt.at(0)==MSG_SELECT_CHAIN)submit(Origin::Automatic,integer(0xffffffffu));
                else if(prompt.at(0)==MSG_SELECT_PLACE)submit(Origin::Automatic,place(prompt));
                else throw std::runtime_error("Unexpected real prompt while advancing privacy flow: "+std::to_string(prompt.at(0)));
            }
        };
        reach(MSG_SELECT_IDLECMD);
        CHECK(duel.CurrentBoundary().checkpoint.player==0);
        submit(Origin::Manual,integer(idleCommand(duel.CurrentBoundary().checkpoint.prompt,4,Upstart)));
        reach(MSG_SELECT_IDLECMD);
        submit(Origin::Manual,integer(7));
        reach(MSG_SELECT_IDLECMD);
        CHECK(duel.CurrentBoundary().checkpoint.player==1);
        submit(Origin::Manual,integer(idleCommand(duel.CurrentBoundary().checkpoint.prompt,5,Mst)));
        reach(MSG_SELECT_CARD);
        const auto privatePrompt=duel.CurrentBoundary().checkpoint.prompt;
        const auto historicalPrompt=duel.Status().prompt;
        CHECK(privatePrompt.at(1)==1 && privatePrompt.at(5)==1);
        CHECK(word(privatePrompt,6)==Upstart && privatePrompt.at(10)==0 && privatePrompt.at(11)==LOCATION_SZONE);
        std::array<Bytes,2> publicPrompt;
        for(unsigned slot=0;slot<2;++slot) {
            const auto selecting=(slot==0?host.type:peer.type)==1;
            for(const auto& item:visible[slot])
                if(item.prompt==historicalPrompt && item.frame.at(0)==(selecting?MSG_SELECT_CARD:MSG_WAITING))publicPrompt[slot]=item.frame;
            CHECK(!publicPrompt[slot].empty());
            if(selecting) {
                CHECK(word(publicPrompt[slot],6)==0);
                auto expected=privatePrompt;for(unsigned i=6;i<10;++i)expected[i]=0;
                CHECK(publicPrompt[slot]==expected);
            } else CHECK(publicPrompt[slot]==Bytes{MSG_WAITING});
        }
        submit(Origin::Manual,Bytes{1,0});
        reach(MSG_SELECT_IDLECMD); // Subsequent real chain choices are Automatic.
        const auto target=duel.History().Target(second->type);CHECK(target.has_value());
        CHECK(duel.History().Records().at(*target).before.prompt==privatePrompt);
        TxKey request{session,duel.InstalledEpoch(),duel.Status().nextRequest,0,{}};
        duel.ReceiveUndo(second,{WireKind::Request,request,{}});
        CHECK(duel.Status().state==TxState::Preparing);
        const auto key=duel.ActiveKey();CHECK(key.targetIndex==*target);
        for(unsigned slot=0;slot<2;++slot) {
            FragmentAssembler assembler;
            for(const auto& envelope:envelopes[slot])
                if(envelope.kind==WireKind::Prepare && SameKey(envelope.key,key))assembler.Add(envelope.payload);
            const auto descriptor=DecodeRoomRestore(assembler.Finish());
            const auto restored=DecodePlayerRestore(descriptor.visible);
            CHECK(descriptor.prompt==historicalPrompt);
            CHECK(restored.player==(slot==0?host.type:peer.type));
            const bool matches=restored.prompt==publicPrompt[slot];
            everyRecipientMatched=everyRecipientMatched&&matches;
            std::cout<<"swapped="<<swapped<<" endpoint="<<slot+1<<" engine="<<unsigned(restored.player)
                <<" exact filtered restore="<<matches<<'\n';
        }
    }
    CHECK(everyRecipientMatched);
    std::cout<<"Actual MST hidden-Upstart target preserves both recipient prompts across both engine-seat orientations\n";
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;} }