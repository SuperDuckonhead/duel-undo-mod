#include "core_fixture.h"
#include "undo/core_driver.h"
#include "ocgapi.h"
#include <iostream>
using namespace undo;
int main(){try {
 const auto root=(std::filesystem::current_path()/"core-lookup-fixture").u8string();
 fixture::database(root);
 for(const auto* n:{"constant.lua","utility.lua","procedure.lua"})fixture::WriteFixtureFile(root+"/script/"+n,{});
 fixture::WriteFixtureFile(root+"/single/lookup.lua",fixture::bytes(R"lua(
local e=Effect.GlobalEffect()
e:SetType(0x802)
e:SetCode(1040)
e:SetOperation(function(e)
 e:Reset()
 Duel.SelectYesNo(0,100)
 local c=Duel.CreateToken(0,900000001)
 local add=Effect.CreateEffect(c)
 add:SetType(1)
 add:SetCode(113)
 add:SetValue(10000050)
 c:RegisterEffect(add)
 local expected=UNDO_SCENARIO_PARAMETERS=="present"
 if c:IsSetCard(0x10a2)~=expected then error("Frozen optional card lookup changed") end
 Duel.SelectYesNo(1,200)
end)
Duel.RegisterEffect(e,0)
)lua"));
 auto frozen=ResourceView::Capture(root);
 CHECK(frozen->Cards().count(10000050)==0);
 InitialState initial;initial.seed.resize(SEED_COUNT,19);initial.duelOptions=5u<<16;
 initial.resourceDigest=frozen->Fingerprint();initial.scenarioName="single/lookup.lua";
 for(std::uint8_t p=0;p<2;++p)for(int i=0;i<5;++i)initial.cards.push_back({900000001,p,p,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE});
 auto live=CoreDriver::Create(initial,frozen);auto first=live->Advance();CHECK(first.kind==BoundaryKind::AwaitResponse);
 // A matching physical DB row appears after capture. The old view still means absent.
 fixture::sql(root+"/cards.cdb","INSERT INTO datas SELECT 10000050,ot,alias,4258,type,atk,def,level,race,attribute,category FROM datas WHERE id=900000001; INSERT INTO texts(id,name) VALUES(10000050,'optional virtual name');");
 live->Submit({1,0,0,0});auto next=live->Advance();
 if(next.kind!=BoundaryKind::AwaitResponse)throw std::runtime_error(next.failure);
 CHECK(next.checkpoint.player==1);
 auto replay=CoreDriver::Create(initial,frozen);replay->Advance();replay->Submit({1,0,0,0});auto rebuilt=replay->Advance();
 CHECK(rebuilt.kind==next.kind && rebuilt.checkpoint.prompt==next.checkpoint.prompt && rebuilt.checkpoint.canonicalState==next.checkpoint.canonicalState && rebuilt.checkpoint.transcriptDigest==next.checkpoint.transcriptDigest);
 auto newer=ResourceView::Capture(root);CHECK(newer->Fingerprint()!=frozen->Fingerprint());
 auto changed=initial;changed.resourceDigest=newer->Fingerprint();changed.scenarioParameters=fixture::bytes("present");
 auto actualPresent=CoreDriver::Create(changed,newer);actualPresent->Advance();actualPresent->Submit({1,0,0,0});CHECK(actualPresent->Advance().kind==BoundaryKind::AwaitResponse);
 bool missingInitial=false;auto invalid=initial;invalid.cards.front().code=10000050;
 try{CoreDriver::Create(invalid,frozen);}catch(const std::exception&){missingInitial=true;}CHECK(missingInitial);
 bool strict=false;try{frozen->Card(10000050);}catch(const std::exception&){strict=true;}CHECK(strict);
 std::cout<<"actual EFFECT_ADD_CODE optional set lookup preserves frozen absence/presence; initial cards remain strict; replay identical\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
