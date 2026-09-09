#include "bot_controller.h"
#include "../data_manager.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <filesystem>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <bcrypt.h>

namespace undo {
namespace {
void put(Bytes& b, std::uint64_t value, unsigned width) { for(unsigned i=0;i<width;i++) b.push_back(static_cast<std::uint8_t>(value>>(i*8))); }
template<std::size_t N> void put(Bytes& b,const std::array<std::uint8_t,N>& data) { b.insert(b.end(),data.begin(),data.end()); }
void blob(Bytes& b,const Bytes& data) { put(b,data.size(),4);b.insert(b.end(),data.begin(),data.end()); }
void text(Bytes& b,const std::string& value) { blob(b,Bytes(value.begin(),value.end())); }
void key(Bytes& b,const TxKey& k) { put(b,k.session);put(b,k.epoch,8);put(b,k.request,8);put(b,k.targetIndex,8);put(b,k.targetDigest); }
std::string utf8(const std::wstring& value) {
 if(value.empty())return {};
 int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
 if(!size)throw std::runtime_error("Invalid card text encoding");
 std::string result(size,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),size,nullptr,nullptr);return result;
}
void coreCard(Bytes& b,const card_data& c) {
 put(b,c.code,4);put(b,c.alias,4);for(auto value:c.setcode)put(b,value,2);
 for(auto value:{c.type,c.level,c.attribute,c.race,static_cast<std::uint32_t>(c.attack),static_cast<std::uint32_t>(c.defense),c.lscale,c.rscale,c.link_marker,c.rule_code})put(b,value,4);
}
struct Reader {
 const Bytes& bytes; std::size_t at{};
 void require(std::size_t n) { if(n>bytes.size()-at)throw std::runtime_error("Truncated bot control result"); }
 std::uint64_t get(unsigned width) { require(width);std::uint64_t n=0;for(unsigned i=0;i<width;i++)n|=std::uint64_t(bytes[at++])<<(i*8);return n; }
 template<std::size_t N> void array(std::array<std::uint8_t,N>& a) { require(N);std::copy_n(bytes.begin()+at,N,a.begin());at+=N; }
 Bytes data() { auto n=get(4);require(n);Bytes b(bytes.begin()+at,bytes.begin()+at+n);at+=n;return b; }
 std::string string() { auto b=data();return std::string(b.begin(),b.end()); }
 void end() { if(at!=bytes.size())throw std::runtime_error("Trailing bot control result"); }
};
struct Handle {
 HANDLE value{INVALID_HANDLE_VALUE};
 Handle()=default;explicit Handle(HANDLE h):value(h){}
 ~Handle(){if(value && value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
 Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
 void reset(HANDLE h=INVALID_HANDLE_VALUE){if(value && value!=INVALID_HANDLE_VALUE)CloseHandle(value);value=h;}
};
struct LocalMemory { void* value{}; ~LocalMemory(){if(value)LocalFree(value);} };
void winCheck(bool success,const char* message) { if(!success)throw std::runtime_error(std::string(message)+" (Windows "+std::to_string(GetLastError())+")"); }
std::wstring quote(const std::wstring& input) {
 std::wstring output=L"\"";unsigned slash=0;
 for(wchar_t c:input){if(c==L'\\'){++slash;continue;}output.append(c==L'"'?slash*2+1:slash,L'\\');output+=c;slash=0;}
 output.append(slash*2,L'\\');return output+L"\"";
}
std::wstring userSid() {
 HANDLE raw{};winCheck(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&raw)!=0,"OpenProcessToken");Handle token(raw);
 DWORD size{};GetTokenInformation(token.value,TokenUser,nullptr,0,&size);Bytes bytes(size);
 winCheck(GetTokenInformation(token.value,TokenUser,bytes.data(),size,&size)!=0,"GetTokenInformation");
 LPWSTR sid{};winCheck(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid,&sid)!=0,"ConvertSid");LocalMemory memory{sid};return sid;
}
}
Bytes CaptureBotCardView(const ResourceView& view,const ygo::DataManager& data,const Digest& engine) {
 if(view.Cards().size()!=data.GetDataTable().size())throw std::runtime_error("Bot resource capture must share the core initialization snapshot");
 Bytes out;put(out,1,4);put(out,engine);put(out,view.Fingerprint());put(out,view.Cards().size(),4);
 for(auto& item:view.Cards()) {
  card_data current{};if(!data.GetData(item.first,&current))throw std::runtime_error("Missing normalized bot card");
  Bytes before,now;coreCard(before,item.second);coreCard(now,current);if(before!=now)throw std::runtime_error("Core/DataManager views differ at bot capture");
  out.insert(out.end(),before.begin(),before.end());put(out,data.GetDataTable().at(item.first).ot,4);
  ygo::CardString strings{};data.GetString(item.first,&strings);
  text(out,utf8(strings.name));text(out,utf8(strings.text));for(auto& desc:strings.desc)text(out,utf8(desc));
 }
 if(out.size()+32>32*1024*1024)throw std::runtime_error("Bot resource bridge exceeds limit");
 put(out,Sha256(out));return out;
}
struct BotController::Impl {
 Handle pipe, process, job; BotCancellation cancel; BotSelectionInfo selection;
 SessionId session{};BotState state{BotState::Frozen};std::uint64_t epoch{},cursor{};
 std::uint32_t active{},candidate{},retained{},commits{};std::string failure;
 std::vector<BotOutput> outputs;
 ~Impl(){ job.reset(); if(process.value!=INVALID_HANDLE_VALUE)WaitForSingleObject(process.value,5000); }
 DWORD complete(OVERLAPPED& overlapped) {
  DWORD result=WAIT_TIMEOUT; const auto started=GetTickCount64();
  do {
   if(cancel && cancel->load())break;
   result=WaitForSingleObject(overlapped.hEvent,50);
  } while(result==WAIT_TIMEOUT && GetTickCount64()-started<30000);
  if(result!=WAIT_OBJECT_0){CancelIoEx(pipe.value,&overlapped);WaitForSingleObject(overlapped.hEvent,INFINITE);throw std::runtime_error("Bot private pipe timed out; participant remains paused");}
  DWORD bytes{};winCheck(GetOverlappedResult(pipe.value,&overlapped,&bytes,FALSE)!=0,"Bot pipe transfer");return bytes;
 }
 void transfer(void* buffer,std::size_t size,bool writing) {
  auto* bytes=static_cast<std::uint8_t*>(buffer);
  while(size){if(cancel && cancel->load())throw std::runtime_error("Bot operation cancelled");Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));winCheck(event.value!=nullptr,"Create pipe event");OVERLAPPED ov{};ov.hEvent=event.value;DWORD done{};
   DWORD chunk=static_cast<DWORD>(std::min<std::size_t>(size,65536));BOOL ok=writing?WriteFile(pipe.value,bytes,chunk,&done,&ov):ReadFile(pipe.value,bytes,chunk,&done,&ov);
   if(!ok){if(GetLastError()!=ERROR_IO_PENDING)winCheck(false,"Bot pipe I/O");done=complete(ov);} if(!done)throw std::runtime_error("Bot pipe closed");bytes+=done;size-=done;
  }
 }
 bool request(Bytes payload) {
  if(state==BotState::Failed)return false;
  try {
   if(payload.size()>64*1024*1024)throw std::runtime_error("Bot request too large");Bytes length;put(length,payload.size(),4);transfer(length.data(),4,true);transfer(payload.data(),payload.size(),true);
   std::uint8_t success{};transfer(&success,1,false);if(success!=1)throw std::runtime_error("Bot control rejected command; participant paused");
   Bytes header(4);transfer(header.data(),4,false);Reader h{header};auto size=h.get(4);if(size>64*1024*1024)throw std::runtime_error("Bot response too large");
   Bytes body(size);transfer(body.data(),body.size(),false);Reader r{body};bool accepted=r.get(1)!=0;
   auto nextState=r.get(1);if(nextState>static_cast<unsigned>(BotState::Failed))throw std::runtime_error("Invalid bot state");
   auto nextEpoch=r.get(8);auto nextActive=r.get(4),nextCandidate=r.get(4),nextRetained=r.get(4);auto nextCursor=r.get(8),nextCommits=r.get(4);auto nextFailure=r.string();
   BotSelectionInfo selected; selected.name=r.string();selected.executor=r.string();selected.deckFile=r.string();selected.dialog=r.string();selected.hand=r.get(4);selected.chat=r.get(1)!=0;selected.usePreErrataEffects=r.get(1)!=0;selected.customDeckSource=r.string();
   std::vector<BotOutput> received;auto count=r.get(4);if(count>1000000)throw std::runtime_error("Invalid bot output count");
   for(std::size_t i=0;i<count;i++){BotOutput out;r.array(out.session);out.epoch=r.get(8);out.prompt=r.get(8);out.origin=static_cast<Origin>(r.get(1));out.producerPid=r.get(4);out.packet=r.data();received.push_back(std::move(out));}r.end();
   state=static_cast<BotState>(nextState);epoch=nextEpoch;active=nextActive;candidate=nextCandidate;retained=nextRetained;cursor=nextCursor;commits=nextCommits;failure=std::move(nextFailure);selection=std::move(selected);outputs=std::move(received);return accepted;
  } catch(const std::exception& e){state=BotState::Failed;failure=e.what();outputs.clear();return false;}
 }
};
BotController::BotController(const std::wstring& executable,const BotLaunchData& init,SessionId session,std::uint64_t epoch,BotCancellation cancel):impl_(new Impl) {
 auto& p=*impl_;p.session=session;p.epoch=epoch;p.cancel=std::move(cancel);if(p.cancel && p.cancel->load())throw std::runtime_error("Bot initialization cancelled");
 std::array<unsigned char,24> random{};winCheck(BCryptGenRandom(nullptr,random.data(),random.size(),BCRYPT_USE_SYSTEM_PREFERRED_RNG)==0,"Bot pipe random");
 std::wstring name=L"ygopro-undo-";const wchar_t* hex=L"0123456789abcdef";for(auto byte:random){name+=hex[byte>>4];name+=hex[byte&15];}
 const auto sid=userSid();std::wstring sddl=L"O:"+sid+L"D:P(D;;GA;;;NU)(A;;GA;;;"+sid+L")";
 PSECURITY_DESCRIPTOR descriptor{};winCheck(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&descriptor,nullptr)!=0,"Bot pipe ACL");LocalMemory security{descriptor};
 SECURITY_ATTRIBUTES attributes{sizeof(attributes),descriptor,FALSE};
 p.pipe.reset(CreateNamedPipeW((L"\\\\.\\pipe\\"+name).c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,65536,65536,0,&attributes));winCheck(p.pipe.value!=INVALID_HANDLE_VALUE,"Create private bot pipe");

 p.job.reset(CreateJobObjectW(nullptr,nullptr));winCheck(p.job.value!=nullptr,"Create bot process job");JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;winCheck(SetInformationJobObject(p.job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits))!=0,"Configure bot process job");
 std::wstring command=quote(executable)+L" --undo-control --control-pipe "+name;STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
 auto cwd=std::filesystem::path(executable).parent_path().wstring();winCheck(CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,cwd.c_str(),&startup,&process)!=0,"Start bot control process");p.process.reset(process.hProcess);Handle thread(process.hThread);
 if(!AssignProcessToJobObject(p.job.value,p.process.value)){TerminateProcess(p.process.value,1);winCheck(false,"Assign bot process job");}winCheck(ResumeThread(thread.value)!=static_cast<DWORD>(-1),"Resume bot control process");
 Handle connected(CreateEventW(nullptr,TRUE,FALSE,nullptr));OVERLAPPED ov{};ov.hEvent=connected.value;BOOL ready=ConnectNamedPipe(p.pipe.value,&ov);if(!ready){auto error=GetLastError();if(error==ERROR_IO_PENDING)p.complete(ov);else if(error!=ERROR_PIPE_CONNECTED)winCheck(false,"Listen bot pipe");} ULONG clientPid{};winCheck(GetNamedPipeClientProcessId(p.pipe.value,&clientPid)!=0 && clientPid==process.dwProcessId,"Authenticate bot control PID");
 Bytes request{1};put(request,session);put(request,epoch,8);put(request,init.engine);put(request,init.resources);blob(request,init.cardView);
 text(request,init.runtimeRoot);text(request,init.executor);text(request,init.deckFile);text(request,init.dialog);put(request,static_cast<std::uint32_t>(init.seed),4);put(request,init.chat,1);put(request,init.usePreErrataEffects,1);
 text(request,init.name);put(request,init.hand,4);text(request,init.selectionCommand);blob(request,init.selectionCatalog);text(request,init.customDeckSource);put(request,init.hasCustomDeck,1);if(init.hasCustomDeck)blob(request,init.customDeck);
 if(!p.request(std::move(request)))throw std::runtime_error(p.failure);
}
BotController::~BotController()=default;
std::vector<BotOutput> BotController::Dispatch(SessionId session,std::uint64_t epoch,std::uint64_t prompt,const Bytes& packet) {
 auto& p=*impl_;if(p.state!=BotState::Running || session!=p.session || epoch!=p.epoch)return {};
 Bytes request{2};put(request,session);put(request,epoch,8);put(request,prompt,8);blob(request,packet);if(!p.request(std::move(request)))return {};
 auto output=std::move(p.outputs);p.outputs.clear();std::vector<BotOutput> accepted;
 for(auto& item:output)if(AcceptsBotOutput(item,Identity(),prompt))accepted.push_back(std::move(item));return accepted;
}
bool BotController::Prepare(const TxKey& tx,std::size_t cursor){Bytes request{3};key(request,tx);put(request,cursor,8);return impl_->request(std::move(request));}
bool BotController::Commit(const TxKey& tx){Bytes request{4};key(request,tx);return impl_->request(std::move(request));}
void BotController::Abort(const TxKey& tx){Bytes request{5};key(request,tx);impl_->request(std::move(request));}
bool BotController::Resume(const TxKey& tx,std::uint64_t epoch){Bytes request{6};key(request,tx);put(request,epoch,8);return impl_->request(std::move(request));}
void BotController::Pause(){impl_->request(Bytes{8});impl_->state=BotState::Failed;}
bool BotController::Deliver(const BotOutput& output,std::uint64_t prompt,const std::function<void(const Bytes&)>& sink) const {
 if(!AcceptsBotOutput(output,Identity(),prompt))return false;sink(output.packet);return true;
}
bool AcceptsBotOutput(const BotOutput& output,const BotIdentity& identity,std::uint64_t prompt) noexcept {
 return identity.state==BotState::Running && identity.activePid!=0 && output.session==identity.session && output.epoch==identity.epoch && output.prompt==prompt && output.origin==Origin::Bot && output.producerPid==identity.activePid;
}
BotIdentity BotController::Identity()const{return {impl_->session,impl_->epoch,impl_->state,impl_->active};}
const BotSelectionInfo& BotController::Selection()const{return impl_->selection;}
std::size_t BotController::Cursor()const{return static_cast<std::size_t>(impl_->cursor);}
BotState BotController::State()const{return impl_->state;}
std::uint64_t BotController::Epoch()const{return impl_->epoch;}
std::uint32_t BotController::ActivePid()const{return impl_->active;}
std::uint32_t BotController::CandidatePid()const{return impl_->candidate;}
std::uint32_t BotController::RetainedPid()const{return impl_->retained;}
std::uint32_t BotController::CommitCount()const{return impl_->commits;}
const std::string& BotController::Failure()const{return impl_->failure;}
}