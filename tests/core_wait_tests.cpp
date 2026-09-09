#include "core_fixture.h"
#include "undo/core_driver.h"
#include "ocgapi.h"
#include <algorithm>
#include <iostream>
using namespace undo;
static InitialState start(const std::shared_ptr<const ResourceView>& view,const std::string& mode) {
    InitialState initial;initial.seed.resize(SEED_COUNT,19);initial.duelOptions=5u<<16;
    initial.resourceDigest=view->Fingerprint();initial.scenarioName="single/wait.lua";initial.scenarioParameters=fixture::bytes(mode);
    for(std::uint8_t p=0;p<2;++p)for(int i=0;i<5;++i)initial.cards.push_back({900000001,p,p,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE});
    initial.cards.push_back({900000002,0,0,LOCATION_EXTRA,0,POS_FACEDOWN_DEFENSE});
    return initial;
}
int main() {try {
    const auto root=(std::filesystem::current_path()/"core-wait-fixture").u8string();
    fixture::database(root);
    fixture::sql(root+"/cards.cdb","INSERT INTO datas SELECT 900000002,ot,alias,setcode,65,atk,def,level,race,attribute,category FROM datas WHERE id=900000001; INSERT INTO texts(id,name) VALUES(900000002,'fusion fixture');");
    for(const auto* name:{"constant.lua","utility.lua","procedure.lua"})fixture::WriteFixtureFile(root+"/script/"+name,{});
    fixture::WriteFixtureFile(root+"/single/wait.lua",fixture::bytes(R"lua(
local e=Effect.GlobalEffect()
e:SetType(0x802)
e:SetCode(1040)
e:SetOperation(function(e)
 e:Reset()
 Duel.SelectYesNo(0,100)
 if UNDO_SCENARIO_PARAMETERS=="cards" then Duel.ConfirmCards(0,Duel.GetDecktopGroup(0,1))
 elseif UNDO_SCENARIO_PARAMETERS=="deck" then Duel.ConfirmDecktop(0,1)
 elseif UNDO_SCENARIO_PARAMETERS=="extra" then Duel.ConfirmExtratop(0,1)
 elseif UNDO_SCENARIO_PARAMETERS=="bounded" then
  for i=1,100001 do Duel.ConfirmCards(0,Duel.GetDecktopGroup(0,1)) end
 end
 Duel.SelectYesNo(1,200)
end)
Duel.RegisterEffect(e,0)
)lua"));
    fixture::WriteFixtureFile(root+"/script/c900000002.lua",fixture::bytes("function c900000002.initial_effect(c) end"));
    const auto view=ResourceView::Capture(root);
    bool passed=true;
    for(const std::string mode:{"cards","deck","extra"}) {
        const auto initial=start(view,mode);auto live=CoreDriver::Create(initial,view);
        auto first=live->Advance();CHECK(first.kind==BoundaryKind::AwaitResponse && first.checkpoint.prompt.at(0)==MSG_SELECT_YESNO);
        live->Submit({1,0,0,0});std::vector<std::uint8_t> events;
        auto next=live->Advance([&](const Bytes& bytes){events.push_back(bytes.at(0));});
        const auto expected=mode=="cards"?MSG_CONFIRM_CARDS:mode=="deck"?MSG_CONFIRM_DECKTOP:MSG_CONFIRM_EXTRATOP;
        CHECK(std::count(events.begin(),events.end(),expected)==1);
        if(next.kind!=BoundaryKind::AwaitResponse){std::cerr<<mode<<": "<<next.failure<<'\n';passed=false;continue;}
        CHECK(next.checkpoint.player==1 && next.checkpoint.prompt.at(0)==MSG_SELECT_YESNO);
        CHECK(!next.rejectedResponse);
        auto candidate=CoreDriver::Create(initial,view);candidate->Advance();candidate->Submit({1,0,0,0});
        const auto replay=candidate->Advance();
        CHECK(replay.kind==next.kind && replay.checkpoint.prompt==next.checkpoint.prompt &&
              replay.checkpoint.canonicalState==next.checkpoint.canonicalState && replay.checkpoint.transcriptDigest==next.checkpoint.transcriptDigest);
        std::cout<<"PASS actual "<<mode<<" nonresponse PROCESSOR_WAIT resumes to next prompt and replays identically\n";
    }
    CHECK(passed);
    auto bounded=CoreDriver::Create(start(view,"bounded"),view);bounded->Advance();bounded->Submit({1,0,0,0});
    const auto failed=bounded->Advance();CHECK(failed.kind==BoundaryKind::Failed && failed.failure=="Core boundary processing limit exceeded");
    std::cout<<"PASS repeated actual nonresponse waits remain bounded by the core processing limit\n";
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;} }
