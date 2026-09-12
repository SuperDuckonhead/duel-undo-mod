#include "fusion_gold_fixture.h"
int main(int argc,char** argv) {
 try {
  CHECK(argc==2);auto r=ResourceView::Capture(argv[1]);std::cout<<"resources="<<hex(r->Fingerprint())<<'\n';
  const std::pair<const char*,const char*> pinned[]={
   {"c44362883.lua","df65c2875fe485cab0ba106f2f9a8926af85e5616f2c3aa9d20125becbf85469"},
   {"procedure.lua","df887c18619374f825fc14f5a5f05f6a090c910adbb52933bd8462c773973baa"},
   {"c87746184.lua","8336d726dcdd1720461111283d5a037d7689df96338300ab5843b2a3acb05db8"},
   {"c75500286.lua","45789f7d9fea6da47b798b2d08ac1612521c0ad9d603e9120a8babcb2b867e04"}};
  for(auto& p:pinned){auto hash=hex(Sha256(r->Read(std::string("script/")+p.first)));std::cout<<p.first<<'='<<hash<<'\n';CHECK(hash==p.second);}
  Run fusion(initial(r,44362883),r);fusion.idle();CHECK(fusion.activation(44362883)>=0);
  // Catches missing registration of the actual expansion helper's native
  // target check. The original physical-world closure must have executed.
  auto precheck=PoolQueryGate::Discover(*fusion.core,fusion.prefix,fusion.context());
  CHECK(!precheck.requests.empty());
  auto precheckAgain=PoolQueryGate::Discover(*fusion.core,fusion.prefix,fusion.context());equivalent(precheck.observations,precheckAgain.observations);describe("FUSION PRECHECK",precheck.observations);
  auto& outer=find(precheck.observations,PoolQueryApi::ExistingMatching);CHECK(outer.request.semantic==PoolQuerySemantic::FusionTarget);CHECK(outer.request.source==CardSource::OwnFacedownExtraDeck);CHECK(outer.result==1);
  auto& check=find(precheck.observations,PoolQueryApi::CheckFusion);CHECK(check.result==1);CHECK((codes(check.input)==std::vector<uint32_t>{68468459,89631139}));CHECK(check.additionalCheckPresent && check.additionalGoalPresent);
  // A material query is identified by its target, not a flat call ordinal:
  // adding a second extra-deck candidate must not alias its material scope.
  CHECK(find(precheck.observations,PoolQueryApi::FusionMaterials).request.target==check.request.target);
  auto& procedure=find(precheck.observations,PoolQueryApi::FusionProcedure);CHECK(procedure.result==1);CHECK(procedure.request.handler==outer.request.handler);CHECK(procedure.reasonHandler==check.request.target);CHECK(procedure.reasonRegistration==check.request.procedureRegistration);CHECK(procedure.request.parentScope==check.request.scope);
  for(auto& o:precheck.observations)if(o.request.semantic==PoolQuerySemantic::ExtraMaterial){CHECK(o.members.empty());CHECK(o.request.scope!=outer.request.scope);CHECK(o.request.parentScope==outer.request.scope);CHECK(o.request.source==CardSource::ExistingState);}
  CHECK(find(precheck.observations,PoolQueryApi::FusionMaterials).request.selfLocation==(LOCATION_HAND|LOCATION_DECK|LOCATION_MZONE));
  fusion.activate(44362883);CHECK(fusion.now.checkpoint.prompt[0]==MSG_SELECT_CARD);CHECK(u32(fusion.now.checkpoint.prompt,6)==87746184);
  Run first(PoolQueryGate::Recreate(*fusion.core,fusion.prefix,fusion.context()),fusion.prefix);
  Run second(PoolQueryGate::Recreate(*fusion.core,fusion.prefix,fusion.context()),fusion.prefix);
  auto atTarget=PoolQueryGate::Observations(*first.core);equivalent(atTarget,PoolQueryGate::Observations(*second.core));describe("FUSION OPERATION",atTarget);
  CHECK(find(atTarget,PoolQueryApi::MatchingGroup).request.semantic==PoolQuerySemantic::FusionTarget);CHECK((codes(find(atTarget,PoolQueryApi::MatchingGroup).members)==std::vector<uint32_t>{87746184}));
  auto initialCount=actual(*first.core).size();finishFusion(first);finishFusion(second);
  auto completed=PoolQueryGate::Observations(*first.core);equivalent(completed,PoolQueryGate::Observations(*second.core));describe("FUSION COMPLETED",completed);
  auto& chosen=find(completed,PoolQueryApi::SelectedFusion);CHECK(chosen.result==1);CHECK((codes(chosen.members)==std::vector<uint32_t>{68468459,89631139}));CHECK(chosen.additionalCheckPresent && chosen.additionalGoalPresent);
  CHECK(chosen.request.handler==outer.request.handler);CHECK(chosen.request.stage==PoolQueryStage::ResolutionSelection);CHECK(chosen.request.role==SelectionRole::ResolutionMaterial);
  CHECK(actual(*first.core).size()==initialCount);CHECK(first.now.checkpoint.canonicalState==second.now.checkpoint.canonicalState);
  finishFusion(fusion);CHECK(fusion.core->DiagnosticState()==first.core->DiagnosticState());CHECK(PoolQueryGate::Observations(*fusion.core).empty());
  auto summoned=actual(*first.core,chosen.request.target);CHECK(summoned.location==LOCATION_MZONE);CHECK(summoned.status&STATUS_PROC_COMPLETE);
  for(auto& material:chosen.members){auto c=actual(*first.core,material.instance);CHECK(c.location==LOCATION_GRAVE);auto query=first.core->QueryCard(c.controller,c.location,c.sequence,QUERY_REASON);CHECK((u32(query,8)&(REASON_EFFECT|REASON_MATERIAL|REASON_FUSION))==(REASON_EFFECT|REASON_MATERIAL|REASON_FUSION));}
  Run invalid(initial(r,44362883,false),r);invalid.idle();CHECK(invalid.activation(44362883)<0);
  auto invalidQueries=PoolQueryGate::Discover(*invalid.core,invalid.prefix,invalid.context());CHECK(find(invalidQueries.observations,PoolQueryApi::CheckFusion).result==0);describe("INVALID NO ALBAZ",invalidQueries.observations);
  auto darkState=initial(r,44362883);darkState.cards[1].code=46986414;
  Run dark(darkState,r);dark.idle();CHECK(dark.activation(44362883)<0);auto darkQueries=PoolQueryGate::Discover(*dark.core,dark.prefix,dark.context());CHECK(find(darkQueries.observations,PoolQueryApi::FusionProcedure).result==0);
  // Both targets build their own original material closure. Adding a sibling
  // changes neither the first target's scope nor its per-scope invocation.
  auto twinState=initial(r,44362883);twinState.cards.push_back({87746184,0,0,LOCATION_EXTRA,0,POS_FACEDOWN_DEFENSE});
  Run twins(twinState,r);twins.idle();twins.activate(44362883);
  auto twinsTrace=PoolQueryGate::Discover(*twins.core,twins.prefix,twins.context()).observations;
  auto twinsAgain=PoolQueryGate::Discover(*twins.core,twins.prefix,twins.context()).observations;equivalent(twinsTrace,twinsAgain);
  std::vector<PoolQueryRequest> materialScopes;for(auto& o:twinsTrace)if(o.request.api==PoolQueryApi::FusionMaterials){CHECK((codes(o.members)==std::vector<uint32_t>{68468459,89631139}));materialScopes.push_back(o.request);}
  CHECK(materialScopes.size()==2);CHECK(materialScopes[0].target!=materialScopes[1].target);CHECK(materialScopes[0].scope!=materialScopes[1].scope);
  for(auto& q:materialScopes){CHECK(q.invocation==1);if(q.target==check.request.target)CHECK(q.scope==find(atTarget,PoolQueryApi::FusionMaterials).request.scope);}
  // King's original substitute is legal for Albion under Polymerization but
  // rejected by Branded Fusion's original additional whole-material closure.
  auto substituteState=initial(r,44362883);substituteState.cards[1].location=LOCATION_HAND;substituteState.cards[1].position=POS_FACEUP_ATTACK;
  substituteState.cards[3].code=79109599;substituteState.cards[3].location=LOCATION_HAND;substituteState.cards[3].position=POS_FACEUP_ATTACK;
  Run substitute(substituteState,r);substitute.idle();CHECK(substitute.activation(44362883)<0);
  auto substituteQueries=PoolQueryGate::Discover(*substitute.core,substitute.prefix,substitute.context());auto& rejectedSub=find(substituteQueries.observations,PoolQueryApi::FusionProcedure);CHECK(rejectedSub.result==0);CHECK(rejectedSub.additionalCheckPresent && rejectedSub.additionalGoalPresent);CHECK((codes(rejectedSub.input)==std::vector<uint32_t>{79109599,89631139}));
  auto polymerState=substituteState;polymerState.cards[0].code=24094653;Run polymer(polymerState,r);polymer.idle();CHECK(polymer.activation(24094653)>=0);polymer.activate(24094653);CHECK(polymer.now.checkpoint.prompt[0]==MSG_SELECT_CARD);CHECK(u32(polymer.now.checkpoint.prompt,6)==87746184);
  std::cout<<"two-target scoped membership and original Polymerization-positive / Branded-negative substitute closure passed\n";
  Run gold(initial(r,75500286),r);gold.idle();auto goldPre=PoolQueryGate::Discover(*gold.core,gold.prefix,gold.context());CHECK(find(goldPre.observations,PoolQueryApi::ExistingMatching).result==1);
  gold.activate(75500286);CHECK(gold.now.checkpoint.prompt[0]==MSG_SELECT_CARD);CHECK(u32(gold.now.checkpoint.prompt,6)==89631139);
  Run ga(PoolQueryGate::Recreate(*gold.core,gold.prefix,gold.context()),gold.prefix),gb(PoolQueryGate::Recreate(*gold.core,gold.prefix,gold.context()),gold.prefix);
  auto goldQueries=PoolQueryGate::Observations(*ga.core);equivalent(goldQueries,PoolQueryGate::Observations(*gb.core));describe("GOLD SELECTION",goldQueries);
  auto selected=find(goldQueries,PoolQueryApi::SelectMatching);CHECK(selected.request.stage==PoolQueryStage::ResolutionSelection);CHECK(selected.request.source==CardSource::OwnMainDeck);CHECK(selected.members.size()==1);auto goldId=selected.members[0].instance;
  ga.submit({1,0});gb.submit({1,0});ga.idle();gb.idle();CHECK(ga.core->DiagnosticState()==gb.core->DiagnosticState());auto removed=actual(*ga.core,goldId);CHECK(removed.code==89631139);CHECK(removed.location==LOCATION_REMOVED);CHECK(removed.position&POS_FACEUP);
  gold.submit({1,0});gold.idle();CHECK(gold.core->DiagnosticState()==ga.core->DiagnosticState());CHECK(PoolQueryGate::Observations(*gold.core).empty());
  CHECK(ResourceView::Capture(argv[1])->Fingerprint()==r->Fingerprint());std::cout<<"independent native fusion/Gold prefixes, legal whole materials, invalid whole materials and same-instance removal passed\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
