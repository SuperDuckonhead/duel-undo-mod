#include "test_support.h"
#include "undo/coordinator.h"
#include <limits>
using namespace undo;
TxKey RequestKey(std::uint64_t request=1,std::uint64_t epoch=7) {
 TxKey k{}; k.session[0]=9; k.epoch=epoch; k.request=request; return k;
}
AuthoritativeTarget Target(std::uint8_t seat=0) { AuthoritativeTarget t{seat,4,{},{{777,888}}}; t.publicDigest[0]=55; return t; }
TxKey Begin(Coordinator& c, bool consent=true, std::int64_t time=1000) {
 auto k=RequestKey(); CHECK(c.Request(k,0,time)); CHECK(c.State()==TxState::WaitBoundary);
 CHECK(!c.AcceptsGameInput(k)); c.Boundary(time,false,Target(),{{111,222}});
 CHECK(c.State()==(consent?TxState::Consent:TxState::Preparing));
 auto bound=c.ActiveKey(); CHECK(bound.targetIndex==4 && bound.targetDigest[0]==55);
 CHECK(!c.AcceptsControl(k)); CHECK(c.AcceptsControl(bound)); return bound;
}
int main() {
 auto request=RequestKey(); Coordinator c(request.session,7,true);
 auto bound=Begin(c); CHECK(!c.Request(RequestKey(2),1,1001));
 CHECK(!c.Consent(bound,0,true,1002)); CHECK(!c.Consent(bound,2,true,1002));
 auto forged=bound; ++forged.targetIndex; CHECK(!c.Consent(forged,1,true,1002));
 CHECK(c.Consent(bound,1,true,1003)); CHECK(c.State()==TxState::Preparing);
 c.Ready(bound,0); c.Ready(bound,0); c.Ready(forged,1); c.Ready(bound,2);
 CHECK(c.State()==TxState::Preparing); c.CommitAck(bound,1,8); CHECK(c.State()==TxState::Preparing);
 c.Ready(bound,1); CHECK(c.State()==TxState::Committing);
 c.CommitAck(bound,0,8); c.CommitAck(bound,0,8); c.CommitAck(bound,1,7);
 CHECK(c.State()==TxState::Committing); c.CommitAck(forged,1,8); CHECK(c.Epoch()==7);
 c.CommitAck(bound,1,8); CHECK(c.State()==TxState::Running && c.Epoch()==8);
 CHECK(c.Clock().remainingMs==Target().clock.remainingMs);
 auto events=c.TakeOutgoing(); CHECK(events.back().kind==WireKind::Resume);
 CHECK(events.back().key.epoch==7 && SameKey(events.back().key,bound));
 CHECK(!c.AcceptsGameInput(bound)); auto current=bound; current.epoch=8; CHECK(c.AcceptsGameInput(current));
 CHECK(c.Request(request,0,1100)); CHECK(c.Epoch()==8 && c.State()==TxState::Running);
 CHECK(c.TakeOutgoing().back().kind==WireKind::Resume);
 c.CommitAck(bound,1,8); CHECK(c.TakeOutgoing().back().kind==WireKind::Resume);
 CHECK(!c.Request(request,1,1101));
 auto next=RequestKey(2,8); CHECK(c.Request(next,0,1102)); c.Boundary(1102,false,Target(),{{400,500}});
 CHECK(c.State()==TxState::Consent); CHECK(!c.Consent(bound,1,true,1103));
 CHECK(!c.Request(request,0,1104)); CHECK(c.State()==TxState::Consent);
 // Equality at 30 seconds expires; waiting time never changes old clock.
 for(bool viaTick : {false,true}) {
  Coordinator t(request.session,7,true); auto k=Begin(t);
  if(viaTick) t.Tick(31000); else CHECK(!t.Consent(k,1,true,31000));
  CHECK(t.State()==TxState::Running && t.Epoch()==7); CHECK((t.Clock().remainingMs==std::array<std::int64_t,2>({111,222})));
  CHECK(!t.Consent(k,1,true,31001)); CHECK(t.TakeOutgoing().back().kind==WireKind::Abort);
 }
 Coordinator edge(request.session,7,true); auto edgeKey=Begin(edge); CHECK(edge.Consent(edgeKey,1,true,30999));
 Coordinator deny(request.session,7,true); auto denyKey=Begin(deny); CHECK(deny.Consent(denyKey,1,false,2000));
 CHECK(deny.State()==TxState::Running); CHECK(deny.Clock().remainingMs[0]==111);
 CHECK(deny.Request(request,0,2001)); CHECK(deny.State()==TxState::Running);
 Coordinator a(request.session,7,false); auto ak=Begin(a,false); a.Fail(ak,false);
 CHECK(a.State()==TxState::Aborting); a.AbortAck(ak,0); a.AbortAck(ak,0); a.AbortAck(forged,1);
 CHECK(a.State()==TxState::Aborting); a.AbortAck(ak,1); CHECK(a.State()==TxState::Running);
 CHECK(a.Epoch()==7 && a.Clock().remainingMs[0]==111);
 for(bool afterCommit : {false,true}) {
  Coordinator p(request.session,7,false); auto k=Begin(p,false);
  if(afterCommit) { p.Ready(k,0); p.Ready(k,1); }
  p.Fail(k,true); CHECK(p.State()==TxState::PausedFailed); CHECK(!p.AcceptsGameInput(k));
  p.CommitAck(k,0,8); p.CommitAck(k,1,8); p.AbortAck(k,0); p.AbortAck(k,1);
  CHECK(p.State()==TxState::PausedFailed); CHECK(!p.Request(RequestKey(2),0,2000));
 }
 Coordinator loss(request.session,7,false); auto lk=Begin(loss,false); loss.Ready(lk,0); loss.Ready(lk,1);
 loss.CommitAck(lk,0,8); loss.Tick(31000); CHECK(loss.State()==TxState::PausedFailed);
 Coordinator prepLoss(request.session,7,false); Begin(prepLoss,false); prepLoss.Tick(31000);
 CHECK(prepLoss.State()==TxState::Aborting); prepLoss.Tick(61000); CHECK(prepLoss.State()==TxState::PausedFailed);
 Coordinator ended(request.session,7,true); CHECK(ended.Request(request,0,1000));
 ended.Boundary(5000,true,Target(),{{1,2}}); CHECK(ended.State()==TxState::Running);
 CHECK(ended.TakeOutgoing().back().kind==WireKind::Abort); CHECK(ended.Request(request,0,5001));
 CHECK(ended.State()==TxState::Running);
 Coordinator wrong(request.session,7,true); CHECK(wrong.Request(request,0,1000));
 wrong.Boundary(1000,false,Target(1),{}); CHECK(wrong.State()==TxState::Running);
 Coordinator absent(request.session,7,true); CHECK(absent.Request(request,0,1000));
 absent.Boundary(1000,false,std::nullopt,{}); CHECK(absent.State()==TxState::Running);
 Coordinator gates(request.session,7,true);
 auto bad=request; bad.targetIndex=4; CHECK(!gates.Request(bad,0,1000));
 bad=request; bad.targetDigest[0]=1; CHECK(!gates.Request(bad,0,1000));
 bad=request; bad.request=0; CHECK(!gates.Request(bad,0,1000));
 bad=request; ++bad.session[0]; CHECK(!gates.Request(bad,0,1000));
 bad=request; bad.epoch=6; CHECK(!gates.Request(bad,0,1000));
 CHECK(!gates.Request(request,2,1000)); CHECK(!gates.Request(request,0,-1));
 CHECK(gates.Request(request,0,1000)); CHECK(gates.Request(request,0,1001)); CHECK(gates.TakeOutgoing().empty());
 gates.Boundary(1001,false,Target(),{}); CHECK(gates.Request(request,0,1002));
 CHECK(gates.TakeOutgoing().size()==2); CHECK(!gates.Consent(gates.ActiveKey(),1,true,999));
 Coordinator full(request.session,UINT64_MAX,false); CHECK(!full.Request(RequestKey(1,UINT64_MAX),0,0));
 Coordinator sequence(request.session,7,true); auto max=RequestKey(UINT64_MAX);
 CHECK(sequence.Request(max,0,0)); sequence.Boundary(0,true,std::nullopt,{});
 CHECK(!sequence.Request(request,0,1)); CHECK(sequence.Request(max,0,1));
 Coordinator near(request.session,7,true); auto nk=Begin(near,true,INT64_MAX-10);
 near.Tick(INT64_MAX); CHECK(near.State()==TxState::Consent); CHECK(near.Consent(nk,1,true,INT64_MAX));
}
