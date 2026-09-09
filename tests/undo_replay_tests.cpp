#include "core_fixture.h"
#include "undo/rebuilder.h"
#include "replay.h"
#include "config.h"
#include "deck_manager.h"
#include "common.h"
#include <iostream>
using namespace undo;
// This codec test never exports a deck; fail if the unrelated GUI export path is reached.
namespace ygo { bool DeckManager::SaveDeckArray(const DeckArray&,const wchar_t*) { throw std::runtime_error("Unexpected deck export in replay codec test"); } }
static void waiting(const Boundary& b){CHECK(b.kind==BoundaryKind::AwaitResponse&&!b.rejectedResponse);}
int main(){try{
 const std::string root=UNDO_REPLAY_FIXTURE;fixture::database(root);
 std::filesystem::remove(std::filesystem::u8path(root+"/script/changed.lua")); // Reset only this owned fault fixture.
 for(auto f:{"constant.lua","utility.lua","procedure.lua"})fixture::WriteFixtureFile(root+"/script/"+f,{});
 fixture::WriteFixtureFile(root+"/single/branch.lua",fixture::bytes(R"lua(
 local e=Effect.GlobalEffect()
 e:SetType(0x802) e:SetCode(1040)
 e:SetOperation(function(e)
  e:Reset()
  local choice=Duel.SelectYesNo(0,123)
  Duel.SetLP(0,(choice and 7000 or 3000)+math.random(1,100)+#UNDO_SCENARIO_PARAMETERS)
  Duel.SelectYesNo(1,456)
  Duel.Win(0,0)
 end)
 Duel.RegisterEffect(e,0)
 )lua"));
 auto resources=ResourceView::Capture(root);InitialState initial;initial.seed.resize(SEED_COUNT,0x12345678);initial.seed[3]=42;
 initial.resourceDigest=resources->Fingerprint();initial.scenarioName="single/branch.lua";initial.scenarioParameters={0,1,2,255};initial.duelOptions=5u<<16;initial.noCheckDeck=true;initial.noShuffleDeck=true;
 for(std::uint8_t p=0;p<2;++p)for(int i=0;i<5;++i)initial.cards.push_back({900000001,p,p,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE});
 auto original=CoreDriver::Create(initial,resources);auto first=original->Advance();waiting(first);
 original->Submit({1,0,0,0});auto oldRoute=original->Advance();waiting(oldRoute);
 DuelHistory history;history.Accept({0,Origin::Manual,{1,0,0,0},first.checkpoint});
 auto live=Rebuild(initial,resources,history.Records(),0,first.checkpoint);history.Truncate(0);
 Bytes alternative(256,0);live->Submit(alternative);auto second=live->Advance();waiting(second);
 CHECK(second.checkpoint.canonicalState!=oldRoute.checkpoint.canonicalState);
 history.Accept({0,Origin::Manual,alternative,first.checkpoint});
 live->Submit({1,0,0,0});auto finish=live->Advance();CHECK(finish.kind==BoundaryKind::Finished);
 history.Accept({1,Origin::Manual,{1,0,0,0},second.checkpoint});
 auto previous=std::filesystem::current_path();std::filesystem::current_path(std::filesystem::u8path(root));
 std::filesystem::create_directories("replay");fixture::WriteFixtureFile("replay/_LastReplay.yrp",fixture::bytes("sentinel"));
 ygo::Replay stored;stored.RecordUndoSingle(initial,history.Records(),L"host",L"peer");
 std::ifstream sentinel("replay/_LastReplay.yrp",std::ios::binary);std::string old((std::istreambuf_iterator<char>(sentinel)),{});CHECK(old=="sentinel");
 CHECK(stored.SaveReplay(L"branch"));
 ygo::Replay loaded;CHECK(loaded.OpenReplay(L"branch.yrp"));CHECK(loaded.pheader.base.flag&REPLAY_UNDO_CORE);
 CHECK(loaded.UndoInitial().seed==initial.seed);CHECK(loaded.UndoInitial().scenarioParameters==initial.scenarioParameters);
 auto playback=loaded.CreateUndoDriver(resources);auto point=playback->Advance();CHECK(SamePosition(point.checkpoint,first.checkpoint));
 Bytes input;CHECK(loaded.ReadUndoResponse(point.checkpoint,input));CHECK(input==alternative);playback->Submit(input);point=playback->Advance();
 CHECK(SamePosition(point.checkpoint,second.checkpoint));CHECK(loaded.ReadUndoResponse(point.checkpoint,input));CHECK(input==Bytes({1,0,0,0}));
 playback->Submit(input);point=playback->Advance();CHECK(point.kind==BoundaryKind::Finished);CHECK(SamePosition(point.checkpoint,finish.checkpoint));CHECK(!loaded.ReadUndoResponse(point.checkpoint,input));
 // The terminal host packet is the exact full retained record, without a file.
 ygo::Replay network;network.RecordUndoDuel(initial,history.Records(),L"host",L"peer");
 auto packet=network.ExportUndoReplay();CHECK(packet.size()<=ygo::MAX_REPLAY_SIZE+sizeof(ygo::ExtendedReplayHeader));
 CHECK(!(network.pheader.base.flag&REPLAY_SINGLE_MODE));
 ygo::Replay memory;CHECK(memory.LoadUndoReplay(packet));CHECK(memory.ExportUndoReplay()==packet);
 CHECK(memory.UndoInitial().seed==initial.seed&&memory.UndoInitial().noCheckDeck&&memory.UndoInitial().noShuffleDeck);
 CHECK(memory.UndoInitial().cards.size()==initial.cards.size());
 auto netCore=memory.CreateUndoDriver(resources);auto netPoint=netCore->Advance();
 CHECK(SamePosition(netPoint.checkpoint,first.checkpoint));
 CHECK(memory.ReadUndoResponse(netPoint.checkpoint,input)&&input==alternative);netCore->Submit(input);netPoint=netCore->Advance();
 CHECK(SamePosition(netPoint.checkpoint,second.checkpoint));
 // Every invalid memory load preserves both the previous bytes and its cursor.
 for(std::size_t cut=0;cut<packet.size();++cut){CHECK(!memory.LoadUndoReplay(Bytes(packet.begin(),packet.begin()+cut)));CHECK(memory.ExportUndoReplay()==packet);}
 for(std::size_t at=0;at<packet.size();++at){auto changed=packet;changed[at]^=1;CHECK(!memory.LoadUndoReplay(changed));CHECK(memory.ExportUndoReplay()==packet);}
 auto wrongMode=packet;wrongMode[offsetof(ygo::ExtendedReplayHeader,base)+offsetof(ygo::ReplayHeader,flag)]^=REPLAY_SINGLE_MODE;CHECK(!memory.LoadUndoReplay(wrongMode));
 auto suffix=packet;suffix.push_back(0);CHECK(!memory.LoadUndoReplay(suffix));
 CHECK(!memory.LoadUndoReplay(Bytes(ygo::MAX_REPLAY_SIZE+sizeof(ygo::ExtendedReplayHeader)+1)));
 CHECK(memory.ReadUndoResponse(netPoint.checkpoint,input)&&input==Bytes({1,0,0,0}));netCore->Submit(input);netPoint=netCore->Advance();
 CHECK(netPoint.kind==BoundaryKind::Finished&&SamePosition(netPoint.checkpoint,finish.checkpoint));
 CHECK(!memory.ReadUndoResponse(netPoint.checkpoint,input));
 CHECK(memory.SaveReplay(L"network-memory"));
 ygo::Replay networkDisk;CHECK(networkDisk.OpenReplay(L"network-memory.yrp"));CHECK(networkDisk.ExportUndoReplay()==packet);
 CHECK(stored.ExportUndoReplay().size()==packet.size());
 ygo::Replay memorySingle;CHECK(memorySingle.LoadUndoReplay(stored.ExportUndoReplay()));CHECK(memorySingle.pheader.base.flag&REPLAY_SINGLE_MODE);
 loaded.Rewind();auto wrong=first.checkpoint;wrong.transcriptDigest[0]^=1;bool rejected=false;try{loaded.ReadUndoResponse(wrong,input);}catch(const std::exception&){rejected=true;}CHECK(rejected);

 // Previously written Single body-v1 files remain readable; mode cannot be
 // reinterpreted as network unless the checksummed v2 body explicitly says so.
 auto oldSingle=stored.ExportUndoReplay();
 const std::size_t bodyVersion=sizeof(ygo::ExtendedReplayHeader)+80+sizeof(ygo::DuelParameters)+4;
 oldSingle[bodyVersion]=1;oldSingle.erase(oldSingle.begin()+bodyVersion+2);
 ygo::ExtendedReplayHeader oldHeader{};std::memcpy(&oldHeader,oldSingle.data(),sizeof(oldHeader));--oldHeader.base.datasize;std::memcpy(oldSingle.data(),&oldHeader,sizeof(oldHeader));
 auto oldChecksum=Sha256(Bytes(oldSingle.begin()+sizeof(oldHeader),oldSingle.end()-32));std::copy(oldChecksum.begin(),oldChecksum.end(),oldSingle.end()-32);
 ygo::Replay bodyV1;CHECK(bodyV1.LoadUndoReplay(oldSingle));CHECK(bodyV1.UndoInitial().seed==initial.seed);
 oldHeader.base.flag&=~REPLAY_SINGLE_MODE;std::memcpy(oldSingle.data(),&oldHeader,sizeof(oldHeader));CHECK(!bodyV1.LoadUndoReplay(oldSingle));
 // Decode every truncated file prefix, reject suffixes and both body/header tampering.
 std::ifstream fullFile("replay/branch.yrp",std::ios::binary);Bytes full((std::istreambuf_iterator<char>(fullFile)),{});fullFile.close();
 for(std::size_t cut=0;cut<full.size();++cut){fixture::WriteFixtureFile("replay/truncated.yrp",Bytes(full.begin(),full.begin()+cut));ygo::Replay broken;CHECK(!broken.OpenReplay(L"truncated.yrp"));}
 auto corrupt=full;corrupt.back()^=1;fixture::WriteFixtureFile("replay/corrupt.yrp",corrupt);ygo::Replay corruptReplay;CHECK(!corruptReplay.OpenReplay(L"corrupt.yrp"));
 corrupt=full;corrupt[offsetof(ygo::ExtendedReplayHeader,seed_sequence)]^=1;fixture::WriteFixtureFile("replay/corrupt-seed.yrp",corrupt);CHECK(!corruptReplay.OpenReplay(L"corrupt-seed.yrp"));
 corrupt=full;corrupt.push_back(0);fixture::WriteFixtureFile("replay/suffix.yrp",corrupt);CHECK(!corruptReplay.OpenReplay(L"suffix.yrp"));
 auto invalid=initial;invalid.scenarioName="../secret.lua";rejected=false;try{stored.RecordUndoSingle(invalid,history.Records(),L"h",L"p");}catch(const std::exception&){rejected=true;}CHECK(rejected);
 std::vector<ResponseRecord> tooLarge(1700,history.Records().front());rejected=false;try{stored.RecordUndoSingle(initial,tooLarge,L"h",L"p");}catch(const std::exception&){rejected=true;}CHECK(rejected);
 CHECK(stored.SaveReplay(L"retained"));std::ifstream retainedFile("replay/retained.yrp",std::ios::binary);Bytes retained((std::istreambuf_iterator<char>(retainedFile)),{});CHECK(retained==full);
 fixture::WriteFixtureFile(root+"/script/changed.lua",fixture::bytes("-- changed logical resource"));auto changed=ResourceView::Capture(root);
 rejected=false;try{loaded.CreateUndoDriver(changed);}catch(const std::exception&){rejected=true;}CHECK(rejected);rejected=false;try{memory.CreateUndoDriver(changed);}catch(const std::exception&){rejected=true;}CHECK(rejected);CHECK(SamePosition(original->Current().checkpoint,oldRoute.checkpoint));

 // The entire memory route, including rejected loads, leaves the legacy file alone.
 std::ifstream unchangedSentinel("replay/_LastReplay.yrp",std::ios::binary);std::string untouched((std::istreambuf_iterator<char>(unchangedSentinel)),{});CHECK(untouched=="sentinel");unchangedSentinel.close();
// Legacy uniform replay save/open/response remains the existing codec.
 ygo::Replay legacy;legacy.BeginRecord();ygo::ExtendedReplayHeader header;header.base.id=REPLAY_ID_YRP2;header.base.version=PRO_VERSION;header.base.flag=REPLAY_UNIFORM;legacy.WriteHeader(header);
 std::uint16_t names[40]{};legacy.WriteData(names,sizeof(names),false);ygo::DuelParameters parameters{8000,0,0,5u<<16};legacy.WriteData(&parameters,sizeof(parameters),false);
 for(int i=0;i<4;++i)legacy.Write<std::uint32_t>(0,false);const unsigned char endturn[4]{7,0,0,0};CHECK(legacy.WriteResponse(endturn,4)==5);legacy.EndRecord();CHECK(legacy.SaveReplay(L"legacy"));
 ygo::Replay oldReplay;CHECK(oldReplay.OpenReplay(L"legacy.yrp"));oldReplay.SkipInfo();unsigned char oldResponse[256]{};CHECK(oldReplay.ReadNextResponse(oldResponse));CHECK(oldResponse[0]==7);CHECK(!oldReplay.ReadNextResponse(oldResponse));
 std::filesystem::current_path(previous);std::cout<<"actual A-undo-B-finish-save-open-play, 256-byte response, parameter/seed fidelity, memory-only Single/network record, transactional memory decoding and legacy playback passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
