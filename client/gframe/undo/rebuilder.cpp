#include "rebuilder.h"
#include <stdexcept>
namespace undo {
bool SamePosition(const Checkpoint& a,const Checkpoint& b) {
 return a.player==b.player && a.prompt==b.prompt &&
  a.canonicalState==b.canonicalState && a.transcriptDigest==b.transcriptDigest;
}
std::unique_ptr<CoreDriver> Rebuild(const InitialState& initial,
 std::shared_ptr<const ResourceView> resources,const std::vector<ResponseRecord>& records,
 std::size_t keep,const Checkpoint& target) {
 if(keep>records.size())throw std::out_of_range("Rebuild prefix out of range");
 auto candidate=CoreDriver::Create(initial,std::move(resources));
 auto boundary=candidate->Advance();
 for(std::size_t i=0;i<keep;++i) {
  if(records[i].player!=records[i].before.player || records[i].player>1 ||
     static_cast<unsigned>(records[i].origin)>static_cast<unsigned>(Origin::Bot))
   throw std::runtime_error("Rebuild response metadata invalid at response "+std::to_string(i));
  if(boundary.kind!=BoundaryKind::AwaitResponse ||
     !SamePosition(boundary.checkpoint,records[i].before))
   throw std::runtime_error("Rebuild prompt diverged at response "+std::to_string(i)+": "+boundary.failure);
  candidate->Submit(records[i].response);
  boundary=candidate->Advance();
  if(boundary.rejectedResponse)
   throw std::runtime_error("Rebuild response rejected at response "+std::to_string(i));
 }
 if(boundary.kind!=BoundaryKind::AwaitResponse || !SamePosition(boundary.checkpoint,target))
  throw std::runtime_error("Rebuild target diverged after "+std::to_string(keep)+" responses: "+boundary.failure);
 return candidate;
}
}
