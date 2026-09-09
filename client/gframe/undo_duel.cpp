#include "undo_duel.h"
#include "netserver.h"
#include "data_manager.h"
#include "undo/rebuilder.h"
#include "undo/player_restore.h"
#include "undo/room_restore.h"
#include "undo/host_bot_seat.h"
#include <deque>
#include "../ocgcore/mtrandom.h"
#include <algorithm>
#include <chrono>
#include <limits>
#include <mutex>
#include <random>
#include <stdexcept>
#include <thread>

namespace ygo {
namespace {
using namespace undo;
std::int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
void require(bool ok, const char* reason) {if(!ok)throw std::runtime_error(reason);}
std::uint64_t readEpoch(const Bytes& bytes) {
    require(bytes.size()==8,"Invalid installed epoch");
    std::uint64_t value{};for(unsigned i=0;i<8;++i)value|=std::uint64_t(bytes[i])<<(8*i);return value;
}
Digest publicTarget(const TxKey& request, std::size_t index, std::uint8_t requester) {
    auto key=request;key.targetIndex=index;key.targetDigest={};
    auto bytes=Encode({WireKind::Request,key,{requester}});
    return Sha256(bytes);
}
struct VisibleBoundary {
    std::array<std::size_t,2> cursor{};
    std::uint64_t prompt{};
    std::array<Bytes,2> prompts;
};
struct RebuildJob {
    std::mutex mutex;
    std::atomic<bool> cancel{false};
    std::thread worker;
    bool done{};
    std::unique_ptr<CoreDriver> core;
    std::string error;
};
struct PreparedHost {
    TxKey key;
    std::unique_ptr<CoreDriver> core; // candidate, then retained original after install
    DuelHistory history;
    std::array<std::vector<Bytes>,2> journal;
    std::vector<VisibleBoundary> boundaries;
    Boundary boundary;
    std::array<Bytes,2> restore;
    std::array<bool,2> ready{};
    bool coreReady{}, committed{}, botCommitQueued{}, botAbortQueued{};
};
}

struct UndoDuel::Impl {
    UndoDuel& owner;
    std::shared_ptr<const RoomConfig> config;
    SessionId session;
    Send send;
    Coordinator coordinator;
    std::array<DuelPlayer*,2> participants{};
    std::array<bool,2> departing{};
    std::array<std::unique_ptr<GamePacketStream>,2> incoming;
    std::array<std::uint64_t,2> sequence{};
    std::uint64_t installedEpoch{}, prompt{}, nextPrompt{1}, nextRequest{1};
    InitialState initial;
    std::unique_ptr<CoreDriver> core;
    Boundary boundary;
    DuelHistory history;
    std::array<std::vector<Bytes>,2> journal;
    std::vector<VisibleBoundary> boundaries;
    std::optional<ResponseRecord> submitted;
    std::unique_ptr<PreparedHost> prepared;
    std::shared_ptr<RebuildJob> job;
    ClockState clock;
    std::int64_t clockObserved{}, lastStatus{};
    std::uint8_t clockPlayer{2}, requester{};
    bool advancing{}, terminalPending{}, finished{}, failed{}, draining{}, transaction{};
    std::string error;
    std::unique_ptr<DuelPlayer> botPlayer;
    std::unique_ptr<HostBotSeat> bot;
    HostBotStatus botStatus;
    std::deque<BotOutput> botOutputs;
    struct HeldPacket { DuelPlayer* player; std::uint8_t opcode; Bytes body; };
    std::vector<HeldPacket> heldHuman;
    std::uint64_t botLastDispatch{}, botFenceJob{};
    bool botAdmission{}, botPolling{}, terminalAuthorized{}, replaySent{};
    std::optional<Envelope> botResume;

    Impl(UndoDuel& o,std::shared_ptr<const RoomConfig> c,SessionId s,Send output)
      :owner(o),config(std::move(c)),session(s),send(std::move(output)),
       coordinator(s,0,config && !config->bot && config->capability.mode==RoomMode::ConsentLan) {
        require(config && config->resources,"Missing frozen undo room resources");
        require(config->resources->Fingerprint()==config->capability.resources,"Room resource fingerprint differs");
        require(session!=SessionId{},"Missing room session");
        if(config->bot) {
            require(!config->botExecutable.empty(),"Missing private bot executable");
            require(config->bot->resources==config->resources->Fingerprint(),"Bot/core resources differ");
            botPlayer=std::make_unique<DuelPlayer>();
            botPlayer->endpointId=std::numeric_limits<std::uint64_t>::max();
            botPlayer->undoPeer.ready=true;botPlayer->type=0xff;
            botStatus.generation=1;
            bot=std::make_unique<HostBotSeat>(config->botExecutable,*config->bot,session,0,botStatus.generation);
        }
    }
    ~Impl() {
        if(bot)bot->Stop();
        if(job) {job->cancel=true;if(job->worker.joinable())job->worker.join();}
    }
    int participant(DuelPlayer* p) const {
        for(int i=0;i<2;++i)if(participants[i]==p && p && p->game==&owner)return i;
        return -1;
    }
    bool open() const {
        return core && !failed && !finished && !advancing && !transaction && !botStatus.humanPromptHeld && boundary.kind==BoundaryKind::AwaitResponse &&
            coordinator.State()==TxState::Running;
    }
    void observeClock() {
        const auto now=nowMs();
        if(clockObserved && clockPlayer<2 && owner.host_info.time_limit && open()) {
            auto& remaining=clock.remainingMs[clockPlayer];
            remaining=std::max<std::int64_t>(0,remaining-(now-clockObserved));
        }
        clockObserved=now;
    }
    RoomStatus status() const {
        RoomStatus s;
        s.state=failed?TxState::PausedFailed:coordinator.State();
        if(!failed && transaction && s.state==TxState::Running)
            s.state=prepared && prepared->committed?TxState::Committing:TxState::Aborting;
        s.prompt=prompt;s.nextRequest=nextRequest;s.clock=clock;s.timePlayer=clockPlayer;
        s.promptPlayer=core && !finished && boundary.kind==BoundaryKind::AwaitResponse?boundary.checkpoint.player:2;
        if(open() && !job && nextRequest!=std::numeric_limits<std::uint64_t>::max())
            for(unsigned p=0;p<2;++p)if(history.Target(p))s.eligibleMask|=1u<<p;
        return s;
    }
    bool emit(int p,const Envelope& e) {
        if(bot && p==1)return true; // Private transaction completion supplies this participant's Ready/Ack.
        if(departing[p])return true; // Terminal teardown does not require delivery to the departed endpoint.
        if(!participants[p])return false;
        return send?send(participants[p],e):NetServer::SendUndoToPlayer(participants[p],e);
    }
    void broadcast(const Envelope& e) {
        for(int p=0;p<2;++p)require(emit(p,e),"Undo control transport failed");
    }
    void publish(bool force=false) {
        if(!core || botStatus.humanPromptHeld || botResume)return; // Never advertise an input boundary before its AI fence/Resume.
        const auto now=nowMs();
        if(!force && now-lastStatus<500)return;
        lastStatus=now;
        Envelope e{WireKind::Status,{session,installedEpoch,0,0,{}},EncodeRoomStatus(status())};
        for(int p=0;p<2;++p)if(participants[p] && participants[p]->undoPeer.ready)require(emit(p,e),"Undo status transport failed");
    }
    void fail(const std::string& why,bool mayHaveCommitted=false) {
        error=why;botAdmission=false;botStatus.failure=why;
        if(job && !mayHaveCommitted)job->cancel=true;
        if(coordinator.State()!=TxState::Running)
            coordinator.Fail(coordinator.ActiveKey(),mayHaveCommitted);
        else failed=true;
        clockPlayer=2;
    }
    void raw(DuelPlayer* player,std::uint8_t opcode,const unsigned char* data,std::size_t size) {
        const auto p=participant(player);require(p>=0,"Unknown undo recipient");
        if(departing[p])return;
        if(bot && player!=botPlayer.get() && botStatus.humanPromptHeld) {
            require(heldHuman.size()<4096,"Held human packet limit exceeded");
            heldHuman.push_back({player,opcode,Bytes(data,data+size)});return;
        }
        Bytes packet{opcode};packet.insert(packet.end(),data,data+size);
        require(sequence[p]!=std::numeric_limits<std::uint64_t>::max(),"Game packet sequence exhausted");
        for(const auto& e:EncodeGamePacket(session,installedEpoch,prompt,++sequence[p],packet))
            require(emit(p,e),"Undo game transport failed");
    }
    void sendGame(int enginePlayer,const Bytes& frame) {
        NetServer::SendBufferToPlayer(owner.players[enginePlayer],STOC_GAME_MSG,
            const_cast<std::uint8_t*>(frame.data()),frame.size());
    }
    void queueBot(std::uint8_t opcode,const unsigned char* data,std::size_t size) {
        if(finished || failed || opcode==STOC_REPLAY)return;
        require(botStatus.initialized,"Bot packet before initialization");
        Bytes packet{opcode};packet.insert(packet.end(),data,data+size);
        const auto id=bot->Dispatch(installedEpoch,prompt,std::move(packet));
        require(id!=0,"Private AI input admission closed");
        botLastDispatch=id;++botStatus.pendingInputs;
    }
    void fenceBot() {
        if(bot && !botFenceJob) {
            botFenceJob=bot->Fence();require(botFenceJob!=0,"Private AI fence unavailable");
            botStatus.fencePending=true;
        }
    }
    void joinBot() {
        if(!bot || !botStatus.initialized || botPlayer->game || !participants[0])return;
        const auto name=BufferIO::DecodeUTF8String(botStatus.selection.name);
        BufferIO::CopyCharArray(name.c_str(),botPlayer->name);
        CTOS_JoinGame join{};join.version=PRO_VERSION;BufferIO::CopyCharArray(owner.pass,join.pass);
        owner.JoinGame(botPlayer.get(),reinterpret_cast<unsigned char*>(&join),false);
        require(botPlayer->game==&owner && botPlayer->type<2,"Private AI could not occupy reserved seat");
    }
    void deliverBot() {
        if(!botAdmission || failed || finished || transaction || coordinator.State()!=TxState::Running)return;
        while(!botOutputs.empty() && botAdmission && !transaction && !failed && !finished) {
            auto output=std::move(botOutputs.front());botOutputs.pop_front();
            if(!AcceptsBotOutput(output,botStatus.identity,prompt))continue;
            auto& packet=output.packet;require(!packet.empty(),"Empty private AI output");
            auto* player=botPlayer.get();auto* data=packet.data()+1;const auto size=packet.size()-1;
            switch(packet[0]) {
            case CTOS_UPDATE_DECK:
                require(!core && size>=8,"Unexpected AI deck update");owner.UpdateDeck(player,data,static_cast<unsigned>(size));break;
            case CTOS_HS_READY:
                require(!core && size==0,"Unexpected AI Ready");owner.PlayerReady(player,true);break;
            case CTOS_HAND_RESULT:
                require(!core && (size==1 || size==4),"Invalid AI hand result");owner.HandResult(player,data[0]);break;
            case CTOS_TP_RESULT:
                require(!core && (size==1 || size==4),"Invalid AI turn choice");owner.TPResult(player,data[0]);break;
            case CTOS_TIME_CONFIRM:
                require(size==0,"Invalid AI time confirmation");owner.TimeConfirm(player);break;
            case CTOS_RESPONSE:
                require(size>0 && size<=256,"Invalid AI response size");
                accept(player,Origin::Bot,Bytes(data,data+size),{session,installedEpoch,output.prompt,0,{}});break;
            case CTOS_CHAT:
                require(size>0 && size<=LEN_CHAT_MSG*sizeof(std::uint16_t) && size%2==0,"Invalid AI chat");
                owner.Chat(player,data,static_cast<int>(size));break;
            case CTOS_SURRENDER:
                require(size==0,"Invalid AI surrender");owner.Surrender(player);break;
            default:throw std::runtime_error("Unexpected private AI command");
            }
        }
        botStatus.pendingOutputs=botOutputs.size();
    }
    void releaseHuman(std::size_t cursor) {
        require(botStatus.humanPromptHeld && !transaction,"Invalid AI prompt fence release");
        boundary.checkpoint.aiLogCursor=cursor;botStatus.fencedCursor=cursor;
        botStatus.humanPromptHeld=false;
        auto packets=std::move(heldHuman);heldHuman.clear();
        for(const auto& packet:packets)raw(packet.player,packet.opcode,packet.body.data(),packet.body.size());
        clockPlayer=boundary.checkpoint.player;clockObserved=nowMs();publish(true);
    }
    void finishResume(const Envelope& e) {
        broadcast(e);
        if(transaction && prepared && prepared->committed && SameKey(e.key,prepared->key)) {
            prepared.reset();resumeClock();transaction=false;botAdmission=bool(bot);
        }
    }
    void pollBot() {
        if(!bot || botPolling)return;
        botPolling=true;
        try {
            for(auto& result:bot->Poll()) {
                if(result.generation!=botStatus.generation)continue;
                botStatus.identity=result.identity;botStatus.cursor=result.cursor;
                botStatus.candidatePid=result.candidatePid;botStatus.retainedPid=result.retainedPid;
                botStatus.commitCount=result.commitCount;botStatus.selection=result.selection;
                if(result.operation==BotOperation::Dispatch && botStatus.pendingInputs)--botStatus.pendingInputs;
                if(finished)continue;
                if(!result.accepted) {
                    fail(result.failure.empty()?"Private AI operation failed":result.failure,
                        result.operation==BotOperation::Commit || result.operation==BotOperation::Resume || (prepared && prepared->committed));
                    continue;
                }
                switch(result.operation) {
                case BotOperation::Initialize:
                    botStatus.initialized=true;botAdmission=true;joinBot();break;
                case BotOperation::Dispatch:
                    require(botOutputs.size()+result.outputs.size()<=4096,"Private AI output queue limit exceeded");
                    for(auto& output:result.outputs)botOutputs.push_back(std::move(output));break;
                case BotOperation::Fence:
                    if(result.job==botFenceJob){botFenceJob=0;botStatus.fencePending=false;}
                    break;
                case BotOperation::Prepare:
                    if(prepared && coordinator.State()==TxState::Preparing){prepared->ready[1]=true;ready();}break;
                case BotOperation::Commit:
                    if(prepared && prepared->committed && coordinator.State()==TxState::Committing)
                        coordinator.CommitAck(prepared->key,1,result.identity.epoch);
                    break;
                case BotOperation::Abort:
                    if(coordinator.State()==TxState::Aborting)coordinator.AbortAck(coordinator.ActiveKey(),1);
                    break;
                case BotOperation::Resume:
                    if(botResume){auto e=std::move(*botResume);botResume.reset();finishResume(e);}break;
                default:break;
                }
                deliverBot();
                if(result.operation==BotOperation::Fence && botStatus.humanPromptHeld && !transaction && !failed) {
                    if(botStatus.pendingInputs==0 && botOutputs.empty() && botLastDispatch<result.job)releaseHuman(result.cursor);
                    else fenceBot();
                }
            }
            deliverBot();
            botStatus.pendingOutputs=botOutputs.size();
            botPolling=false;
        } catch(...) {botPolling=false;throw;}
    }
    void boundaryReady(int player) {
        VisibleBoundary visible{{journal[0].size(),journal[1].size()},prompt};
        require(boundaries.size()==history.Records().size() || boundaries.size()==history.Records().size()+1,
            "Visible boundary and accepted history differ");
        if(boundaries.size()==history.Records().size())boundaries.push_back(visible);
        else boundaries.back()=visible;
        owner.last_response=static_cast<unsigned char>(player);
        sendGame(1-player,{MSG_WAITING});
        if(owner.host_info.time_limit) {
            STOC_TimeLimit limit{};limit.player=static_cast<unsigned char>(player);
            limit.left_time=static_cast<unsigned short>(std::min<std::int64_t>(65535,(clock.remainingMs[player]+999)/1000));
            NetServer::SendPacketToPlayer(owner.players[0],STOC_TIME_LIMIT,limit);
            NetServer::SendPacketToPlayer(owner.players[1],STOC_TIME_LIMIT,limit);
            owner.players[player]->state=CTOS_TIME_CONFIRM;
        } else owner.players[player]->state=CTOS_RESPONSE;
        owner.players[1-player]->state=0xff;
    }
    void freezeRequest() {
        observeClock();clockPlayer=2;
        std::optional<AuthoritativeTarget> target;
        auto* asking=participants[requester];
        if(asking && asking->type<2) {
            const auto keep=history.Target(asking->type);
            if(keep && *keep<boundaries.size()) {
                const auto& checkpoint=history.Records()[*keep].before;
                target=AuthoritativeTarget{requester,*keep,publicTarget(coordinator.ActiveKey(),*keep,requester),checkpoint.clock};
            }
        }
        coordinator.Boundary(nowMs(),finished,target,clock);
    }
    void sendPrepared() {
        for(int p=0;p<2;++p)if(!bot || p!=1)
            for(const auto& fragment:Fragment(prepared->restore[p]))
                require(emit(p,{WireKind::Prepare,prepared->key,fragment}),"Prepare transport failed");
    }
    void prepare() {
        const auto key=coordinator.ActiveKey();
        if(prepared && SameKey(prepared->key,key)){sendPrepared();return;}
        require(!job,"Previous candidate worker has not finished");
        const auto keep=static_cast<std::size_t>(key.targetIndex);
        require(keep<history.Records().size() && keep<boundaries.size(),"Missing target history");
        auto next=std::make_unique<PreparedHost>();next->key=key;
        next->boundary.kind=BoundaryKind::AwaitResponse;
        next->boundary.checkpoint=history.Records()[keep].before;
        for(std::size_t i=0;i<keep;++i)next->history.Accept(history.Records()[i]);
        next->boundaries.assign(boundaries.begin(),boundaries.begin()+keep+1);
        const auto& visible=boundaries[keep];
        for(int p=0;p<2;++p) {
            require(participants[p] && participants[p]->type<2,"Missing target participant");
            require(visible.cursor[p]<=journal[p].size(),"Missing recipient journal");
            next->journal[p].assign(journal[p].begin(),journal[p].begin()+visible.cursor[p]);
            const auto recipient=participants[p]->type;
            // The core checkpoint is private. Restore exactly the prompt that
            // SingleDuel::Analyze already filtered for this endpoint.
            const auto& targetPrompt=visible.prompts[p];
            require(!targetPrompt.empty(),"Missing recipient-visible prompt");
            next->journal[p].push_back(targetPrompt);
            if(bot && p==1)continue;
            auto prefix=next->journal[p];prefix.pop_back();
            auto restore=BuildPlayerRestore(recipient,prefix,targetPrompt);
            RoomRestore descriptor{visible.prompt,next->boundary.checkpoint.player,
                next->boundary.checkpoint.clock,EncodePlayerRestore(restore)};
            next->restore[p]=EncodeRoomRestore(descriptor);
        }
        // All copied history/journal/descriptor allocations precede candidate readiness.
        auto records=history.Records();
        auto capturedInitial=initial;auto resources=config->resources;
        auto target=next->boundary.checkpoint;
        auto work=std::make_shared<RebuildJob>();
        work->worker=std::thread([work,capturedInitial=std::move(capturedInitial),resources=std::move(resources),
                     records=std::move(records),keep,target=std::move(target)]() mutable {
            std::unique_ptr<CoreDriver> candidate;std::string failure;
            try {candidate=RebuildCancellable(capturedInitial,resources,records,keep,target,&work->cancel);}
            catch(const std::exception& e){failure=e.what();}
            catch(...){failure="Candidate worker failed";}
            std::lock_guard<std::mutex> lock(work->mutex);
            work->core=std::move(candidate);work->error=std::move(failure);work->done=true;
        });
        job=std::move(work);prepared=std::move(next);
        if(bot)require(bot->Prepare(key,prepared->boundary.checkpoint.aiLogCursor)!=0,"Private AI Prepare unavailable");
        sendPrepared();
    }
    void ready() {
        if(!prepared || !prepared->coreReady)return;
        const auto key=prepared->key;
        for(std::uint8_t p=0;p<2;++p)if(prepared->ready[p])coordinator.Ready(key,p);
    }
    void commit() {
        require(prepared && prepared->coreReady,"Commit without prepared host");
        if(prepared->committed)return;
        if(bot && !prepared->botCommitQueued) {
            require(bot->Commit(prepared->key)!=0,"Private AI Commit unavailable");prepared->botCommitQueued=true;
        }
        // Everything below is an ownership/value switch. Old branch remains in prepared.
        core.swap(prepared->core);
        std::swap(history,prepared->history);
        journal.swap(prepared->journal);
        boundaries.swap(prepared->boundaries);
        std::swap(boundary,prepared->boundary);
        installedEpoch=prepared->key.epoch+1;
        prompt=boundaries.back().prompt;
        sequence={};
        for(auto& stream:incoming)if(stream)stream->Reset(session,installedEpoch);
        prepared->committed=true;
        if(bot){botOutputs.clear();heldHuman.clear();botStatus.humanPromptHeld=false;}
        clockPlayer=2;
    }
    void resumeClock() {
        clock=coordinator.Clock();clockObserved=nowMs();
        clockPlayer=core && !finished?boundary.checkpoint.player:2;
        if(core && !finished) {
            owner.players[boundary.checkpoint.player]->state=CTOS_RESPONSE;
            owner.players[1-boundary.checkpoint.player]->state=0xff;
        }
    }
    void drain() {
        if(draining)return;
        draining=true;
        try {
            for(;;) {
                auto messages=coordinator.TakeOutgoing();if(messages.empty())break;
                for(auto e:messages) {
                    switch(e.kind) {
                    case WireKind::Prepare:
                        try {prepare();}
                        catch(const std::exception& ex){fail(ex.what());}
                        break;
                    case WireKind::Consent:
                        e.payload={participants[requester]->type};broadcast(e);break;
                    case WireKind::Commit:
                        commit();broadcast(e);break;
                    case WireKind::Resume:
                        if(bot && transaction && prepared && prepared->committed && SameKey(e.key,prepared->key)) {
                            if(!botResume){require(bot->Resume(e.key,installedEpoch)!=0,"Private AI Resume unavailable");botResume=e;}
                        } else finishResume(e);
                        break;
                    case WireKind::Abort: {
                        const bool final=coordinator.State()==TxState::Running;
                        e.payload.push_back(final?1:0);
                        if(bot && !final && (!prepared || !prepared->botAbortQueued)) {
                            require(bot->Abort(e.key)!=0,"Private AI Abort unavailable");
                            if(prepared)prepared->botAbortQueued=true;
                        }
                        if(final && transaction && SameKey(e.key,coordinator.ActiveKey())) {
                            prepared.reset();resumeClock();transaction=false;botAdmission=bool(bot);
                        }
                        broadcast(e);break;
                    }
                    default:break;
                    }
                }
            }
            if(coordinator.State()==TxState::PausedFailed)clockPlayer=2;
            publish(true);
            draining=false;
        } catch(...) {
            draining=false;fail("Undo transaction delivery failed",prepared && prepared->committed);
        }
    }
    void poll() {
        pollBot();
        observeClock();
        coordinator.Tick(nowMs());
        if(job && (coordinator.State()==TxState::Aborting || coordinator.State()==TxState::PausedFailed))job->cancel=true;
        if(job) {
            bool done=false;
            {
                std::lock_guard<std::mutex> lock(job->mutex);
                done=job->done;
                if(done && prepared && coordinator.State()==TxState::Preparing &&
                   SameKey(prepared->key,coordinator.ActiveKey())) {
                    if(job->core) {
                        prepared->core=std::move(job->core);prepared->coreReady=true;ready();
                    } else fail(job->error.empty()?"Candidate reconstruction failed":job->error);
                }
            }
            if(done){if(job->worker.joinable())job->worker.join();job.reset();}
        }
        if(coordinator.State()==TxState::WaitBoundary && !advancing)freezeRequest();
        drain();
        resolveExpired();
    }
    bool resolveExpired() {
        if(!open() || !owner.host_info.time_limit || clockPlayer>1 || clock.remainingMs[clockPlayer]!=0)return false;
        Bytes win{MSG_WIN,static_cast<std::uint8_t>(1-clockPlayer),3};
        clockPlayer=2;
        sendGame(0,win);sendGame(1,win);
        terminalAuthorized=true;owner.EndDuel();owner.DuelEndProc();publish(true);
        return true;
    }
    void accept(DuelPlayer* player,Origin origin,const Bytes& response,const TxKey& key) {
        if(!open() || !IsCurrent(key,session,installedEpoch) || key.request!=prompt ||
           player->type!=boundary.checkpoint.player || player->state!=CTOS_RESPONSE)return;
        observeClock();if(resolveExpired())return;boundary.checkpoint.clock=clock;
        submitted=ResponseRecord{player->type,origin,response,boundary.checkpoint};
        clockPlayer=2;player->state=0xff;
        core->Submit(response);owner.Process();
    }
};

UndoDuel::UndoDuel(bool match,std::shared_ptr<const RoomConfig> config,SessionId session,Send send)
    :SingleDuel(match),impl_(std::make_unique<Impl>(*this,std::move(config),session,std::move(send))) {}
UndoDuel::~UndoDuel()=default;
bool UndoDuel::HasActiveDuel() const {return impl_->core && !impl_->finished;}
bool UndoDuel::CanJoinHuman() const {
    return !impl_->core && (impl_->bot?!impl_->participants[0]:(!impl_->participants[0] || !impl_->participants[1]));
}
std::optional<HostBotStatus> UndoDuel::BotStatus() const {
    if(!impl_->bot)return std::nullopt;
    auto result=impl_->botStatus;result.engineSeat=impl_->botPlayer->type;
    result.lobbyReady=result.engineSeat<2 && ready[result.engineSeat];
    return result;
}
void UndoDuel::JoinGame(DuelPlayer* dp,unsigned char* bytes,bool creator) {
    auto& r=*impl_;
    if(!dp || !dp->endpointId)return;
    if(r.bot && dp!=r.botPlayer.get() && r.participants[0])return;
    int free=-1;
    for(int i=0;i<2;++i) {
        if(r.participants[i]==dp)return;
        if(!r.participants[i] && free<0)free=i;
    }
    if(free<0 || r.core)return;
    if(!creator) {
        require(bytes!=nullptr,"Missing join payload");
        CTOS_JoinGame request{};std::memcpy(&request,bytes,sizeof request);
        BufferIO::NullTerminate(request.pass);wchar_t entered[20]{};
        BufferIO::CopyCharArray(request.pass,entered);
        if((dp->game && dp->type!=0xff) || request.version!=PRO_VERSION || std::wcscmp(entered,pass)) {
            SingleDuel::JoinGame(dp,bytes,false);
            return; // Invalid join may have destroyed dp; do not retain or touch it.
        }
    }
    r.departing[free]=false; // A newly admitted endpoint owns a fresh delivery lifetime.
    r.participants[free]=dp;
    SingleDuel::JoinGame(dp,bytes,creator);
    r.joinBot();
    // A failed join may disconnect and destroy dp. Resolve only through the
    // admission callback before using it again in the real NetServer owner.
}
void UndoDuel::TPResult(DuelPlayer* dp,unsigned char tp) {
    auto& r=*impl_;
    if(!dp || dp->state!=CTOS_TP_RESULT || !players[0] || !players[1] || r.core)return;
    try {
        duel_stage=DUEL_STAGE_DUELING;pplayer[0]=players[0];pplayer[1]=players[1];
        if((tp && dp->type==1) || (!tp && dp->type==0)) {
            std::swap(players[0],players[1]);players[0]->type=0;players[1]->type=1;
            std::swap(pdeck[0],pdeck[1]);
        }
        r.initial.seed.resize(SEED_COUNT);std::random_device random;
        for(auto& seed:r.initial.seed)seed=random();
        mtrandom shuffle(r.initial.seed.data(),r.initial.seed.size());
        if(!host_info.no_shuffle_deck){shuffle.shuffle_vector(pdeck[0].main);shuffle.shuffle_vector(pdeck[1].main);}
        r.initial.noCheckDeck=host_info.no_check_deck;r.initial.noShuffleDeck=host_info.no_shuffle_deck;
        r.initial.duelOptions=std::uint32_t(host_info.duel_rule)<<16;
        if(host_info.no_shuffle_deck)r.initial.duelOptions|=DUEL_PSEUDO_SHUFFLE;
        r.initial.resourceDigest=r.config->resources->Fingerprint();
        for(int p=0;p<2;++p) {
            r.initial.players[p]={host_info.start_lp,host_info.start_hand,host_info.draw_count};
            r.clock.remainingMs[p]=std::int64_t(host_info.time_limit)*1000;
            const auto load=[&](const auto& cards,std::uint8_t location) {
                for(auto card=cards.rbegin();card!=cards.rend();++card)
                    r.initial.cards.push_back({(*card)->code,std::uint8_t(p),std::uint8_t(p),location,0,POS_FACEDOWN_DEFENSE});
            };
            load(pdeck[p].main,LOCATION_DECK);load(pdeck[p].extra,LOCATION_EXTRA);
            r.incoming[p]=std::make_unique<GamePacketStream>(r.session,0);
        }
        r.core=CoreDriver::Create(r.initial,r.config->resources);
        Bytes start{MSG_START,0,host_info.duel_rule};
        BufferIO::VectorWrite<std::int32_t>(start,host_info.start_lp);BufferIO::VectorWrite<std::int32_t>(start,host_info.start_lp);
        for(int p=0;p<2;++p) {
            BufferIO::VectorWrite<std::uint16_t>(start,static_cast<std::uint16_t>(pdeck[p].main.size()));
            BufferIO::VectorWrite<std::uint16_t>(start,static_cast<std::uint16_t>(pdeck[p].extra.size()));
        }
        r.sendGame(0,start);start[1]=1;r.sendGame(1,start);
        RefreshExtra(0);RefreshExtra(1);Process();
    } catch(const std::exception& e){r.fail(e.what());r.drain();}
}
void UndoDuel::Process() {
    auto& r=*impl_;if(!r.core || r.advancing || r.finished)return;
    try {
        require(r.nextPrompt!=std::numeric_limits<std::uint64_t>::max(),"Prompt sequence exhausted");
        const auto previousPrompt=r.prompt;
        r.prompt=r.nextPrompt++;r.advancing=true;
        auto next=r.core->Advance([&](const Bytes& frame) {
            if(!frame.empty() && frame[0]==MSG_NEW_TURN) {
                r.clock.remainingMs.fill(std::int64_t(host_info.time_limit)*1000);
                r.clockObserved=nowMs();
            }
            auto copy=frame;SingleDuel::Analyze(copy.data(),static_cast<unsigned int>(copy.size()));
        });
        r.advancing=false;
        if(next.kind==BoundaryKind::Failed){
            auto failure=next.failure;
            if(r.submitted) {
                failure+=" after player "+std::to_string(r.submitted->player)+" prompt "+std::to_string(r.submitted->before.prompt.at(0))+" response";
                for(auto b:r.submitted->response)failure+=" "+std::to_string(b);
            }
            for(const auto& log:r.core->Logs())failure+="; "+log;
            r.fail(failure);r.submitted.reset();r.drain();return;
        }
        if(next.rejectedResponse) {
            // CoreDriver retains its private checkpoint; the AI cursor belongs to the host fence.
            next.checkpoint.aiLogCursor=r.boundary.checkpoint.aiLogCursor;
            r.prompt=previousPrompt;r.submitted.reset();r.boundary=std::move(next);
            r.boundary.checkpoint.clock=r.clock;
            players[r.boundary.checkpoint.player]->state=CTOS_RESPONSE;
            r.clockPlayer=r.boundary.checkpoint.player;r.clockObserved=nowMs();
            r.sendGame(r.boundary.checkpoint.player,{MSG_RETRY});
            r.drain();return;
        }
        if(r.submitted)r.history.Accept(std::move(*r.submitted));
        r.submitted.reset();r.boundary=std::move(next);
        if(r.terminalPending || r.boundary.kind==BoundaryKind::Finished) {
            r.finished=true;r.clockPlayer=2;r.terminalAuthorized=true;EndDuel();DuelEndProc();r.publish(true);return;
        }
        r.boundary.checkpoint.clock=r.clock;
        r.botStatus.humanPromptHeld=r.bot && players[r.boundary.checkpoint.player]!=r.botPlayer.get();
        auto bytes=r.boundary.checkpoint.prompt;
        SingleDuel::Analyze(bytes.data(),static_cast<unsigned int>(bytes.size()));
        r.clockPlayer=r.botStatus.humanPromptHeld?2:r.boundary.checkpoint.player;r.clockObserved=nowMs();
        if(r.botStatus.humanPromptHeld)r.fenceBot();
        if(r.coordinator.State()==TxState::WaitBoundary)r.freezeRequest();
        r.drain();
    } catch(const std::exception& e){r.advancing=false;r.fail(e.what());r.drain();}
}
void UndoDuel::GetResponse(DuelPlayer*,unsigned char*,unsigned int) {
    // Raw legacy input cannot enter this core. ReceiveUndo authenticates epoch,
    // current prompt, origin and actual participant before accepting a response.
}
void UndoDuel::ReceiveUndo(DuelPlayer* dp,const Envelope& e) {
    auto& r=*impl_;const auto p=r.participant(dp);if(p<0 || !dp->undoPeer.ready || (r.failed && e.kind!=WireKind::Request))return;
    if(e.key.session!=r.session)return;
    if((e.kind==WireKind::Response || e.kind==WireKind::Game) && e.key.epoch!=r.installedEpoch)return;
    if(e.kind!=WireKind::Response && e.kind!=WireKind::Game && e.kind!=WireKind::Request &&
       !SameKey(e.key,r.coordinator.ActiveKey()))return;
    try {
        if(e.kind==WireKind::Request) {
            // Rejections are directed, out-of-band values. Malformed and old
            // epoch traffic never creates a reply or advances the duel clock.
            if(!IsCurrent(e.key,r.session,r.installedEpoch) || !e.key.request ||
               e.key.targetIndex || e.key.targetDigest!=Digest{} || !e.payload.empty())return;
            const auto busy=[&] {return r.coordinator.State()!=TxState::Running ||
                r.transaction || r.job || r.botStatus.humanPromptHeld;};
            const auto reject=[&] {require(r.emit(p,{WireKind::RequestRejected,e.key,
                {static_cast<std::uint8_t>(busy()?1:2)}}),"Request rejection transport failed");};
            if(r.failed || r.finished || !r.core || r.botStatus.humanPromptHeld ||
               ((r.job || r.transaction) && r.coordinator.State()==TxState::Running)) {reject();return;}
            // Existing transactions/cached requests use coordinator retransmit
            // semantics, without observing time or disturbing another request.
            if(r.coordinator.State()!=TxState::Running || e.key.request<r.nextRequest) {
                if(r.coordinator.Request(e.key,static_cast<std::uint8_t>(p),nowMs()))r.drain();
                else reject();
                return;
            }
            if(dp->type>1 || !r.history.Target(dp->type)){reject();return;}
            // A fresh admissible request observes expiry before it freezes input.
            r.observeClock();r.coordinator.Tick(nowMs());if(r.resolveExpired())return;
            if(!r.coordinator.Request(e.key,static_cast<std::uint8_t>(p),nowMs())) {reject();return;}
            if(r.coordinator.State()==TxState::WaitBoundary) {
                r.transaction=true;r.botAdmission=false;
                r.requester=static_cast<std::uint8_t>(p);
                r.nextRequest=e.key.request==std::numeric_limits<std::uint64_t>::max()?e.key.request:e.key.request+1;
                if(!r.advancing)r.freezeRequest();
            }
            r.drain();return;
        }
        r.observeClock();r.coordinator.Tick(nowMs());if(r.resolveExpired())return;
        switch(e.kind) {
        case WireKind::Response: {
            auto response=DecodeResponse(e);r.accept(dp,response.origin,response.response,response.key);break;
        }
        case WireKind::Consent:
            require(e.payload.size()==1 && e.payload[0]<=1,"Invalid consent");
            r.coordinator.Consent(e.key,static_cast<std::uint8_t>(p),e.payload[0]!=0,nowMs());break;
        case WireKind::Ready:
            require(e.payload.size()==1 && e.payload[0]<=1,"Invalid readiness");
            if(r.prepared && SameKey(e.key,r.prepared->key) && r.coordinator.State()==TxState::Preparing) {
                if(e.payload[0]){r.prepared->ready[p]=true;r.ready();}
                else r.fail("Client could not prepare the historical view");
            }
            break;
        case WireKind::CommitAck:
            r.coordinator.CommitAck(e.key,static_cast<std::uint8_t>(p),readEpoch(e.payload));break;
        case WireKind::AbortAck:
            require(e.payload.empty(),"Invalid abort acknowledgement");
            r.coordinator.AbortAck(e.key,static_cast<std::uint8_t>(p));break;
        case WireKind::Game: {
            if(!r.incoming[p])return;
            auto packet=r.incoming[p]->Add(e);
            if(!packet || !r.open() || packet->prompt!=r.prompt)return;
            auto& raw=packet->packet;
            if(raw[0]==CTOS_TIME_CONFIRM && raw.size()==1)TimeConfirm(dp);
            else if(raw[0]==CTOS_SURRENDER && raw.size()==1)Surrender(dp);
            else if(raw[0]==CTOS_CHAT && raw.size()>1 && raw.size()-1<=LEN_CHAT_MSG * sizeof(uint16_t))
                Chat(dp,raw.data()+1,static_cast<int>(raw.size()-1));
            break;
        }
        default:throw std::runtime_error("Unexpected client undo message");
        }
        r.drain();
    } catch(const std::exception& ex){r.fail(ex.what(),r.prepared && r.prepared->committed);r.drain();}
}
void UndoDuel::PollUndo(){impl_->poll();}
bool UndoDuel::RoutePacket(DuelPlayer* dp,std::uint8_t opcode,const unsigned char* body,std::size_t size) {
    auto& r=*impl_;
    if(!r.core) {
        if(r.bot && dp==r.botPlayer.get()){r.queueBot(opcode,body,size);return true;}
        return false;
    }
    const auto p=r.participant(dp);if(p<0)return true;
    if(opcode==STOC_GAME_MSG && size && body[0]!=MSG_RETRY && body[0]!=MSG_WIN) {
        if(r.boundaries.size()==r.history.Records().size()+1 && r.boundaries.back().prompt==r.prompt) {
            const auto expected=dp->type==r.boundary.checkpoint.player?
                r.boundary.checkpoint.prompt.front():MSG_WAITING;
            if(body[0]==expected)r.boundaries.back().prompts[p].assign(body,body+size);
        }
        r.journal[p].emplace_back(body,body+size);
    }
    if(r.bot && dp==r.botPlayer.get())r.queueBot(opcode,body,size);
    else r.raw(dp,opcode,body,size);
    return true;
}
void UndoDuel::WaitforResponse(int player){impl_->boundaryReady(player);}
void UndoDuel::TimeConfirm(DuelPlayer* dp) {
    auto& r=*impl_;
    if(r.open() && dp && dp->type==r.boundary.checkpoint.player && dp->state==CTOS_TIME_CONFIRM)
        dp->state=CTOS_RESPONSE;
}
void UndoDuel::TimerTick(){impl_->poll();}
void UndoDuel::EndDuel() {
    auto& r=*impl_;
    if(r.advancing){r.terminalPending=true;return;}
    r.finished=true;r.clockPlayer=2;
    if(r.terminalAuthorized && !r.replaySent && !r.failed && !r.transaction &&
       r.coordinator.State()==TxState::Running && r.core && players[0] && players[1]) {
        r.replaySent=true;
        wchar_t first[20]{},second[20]{};
        BufferIO::CopyCharArray(players[0]->name,first);BufferIO::CopyCharArray(players[1]->name,second);
        Replay replay;replay.RecordUndoDuel(r.initial,r.history.Records(),first,second);
        const auto bytes=replay.ExportUndoReplay();
        for(auto* player:players)if(player!=r.botPlayer.get())r.raw(player,STOC_REPLAY,bytes.data(),bytes.size());
    }
}
void UndoDuel::Surrender(DuelPlayer* dp) {
    auto& r=*impl_;if(!dp || dp->type>1 || !r.open())return;
    Bytes win{MSG_WIN,static_cast<std::uint8_t>(1-dp->type),0};
    r.sendGame(0,win);r.sendGame(1,win);r.terminalAuthorized=true;EndDuel();DuelEndProc();r.publish(true);
}
void UndoDuel::LeaveGame(DuelPlayer* dp) {
    auto& r=*impl_;const auto seat=r.participant(dp);
    if(seat>=0)r.departing[seat]=true;
    // This is terminal room teardown, never a unilateral undo rollback. In
    // particular an escaped Commit cannot resume the remaining peer.
    if(r.core && !r.finished) {
        r.failed=true;r.error="A participant disconnected";r.clockPlayer=2;
        if(r.job)r.job->cancel=true;
        r.coordinator.Fail(r.coordinator.ActiveKey(),true);
        try {r.publish(true);} catch(...) {}
    }
    try {
        SingleDuel::LeaveGame(dp);
    } catch(...) {
        // A second endpoint can close while termination is being delivered.
        // No exception is allowed to leave a libevent teardown callback.
        NetServer::DisconnectPlayer(dp);
        NetServer::StopServer();
    }
}
void UndoDuel::OnPlayerDisconnected(DuelPlayer* dp) {
    auto& r=*impl_;
    for(auto& participant:r.participants)if(participant==dp)participant=nullptr;
    SingleDuel::OnPlayerDisconnected(dp);
}
void UndoDuel::ToObserver(DuelPlayer*) {}
const InitialState& UndoDuel::Initial() const{return impl_->initial;}
const DuelHistory& UndoDuel::History() const{return impl_->history;}
Boundary UndoDuel::CurrentBoundary() const{return impl_->boundary;}
RoomStatus UndoDuel::Status() const{return impl_->status();}
const TxKey& UndoDuel::ActiveKey() const{return impl_->coordinator.ActiveKey();}
std::uint64_t UndoDuel::InstalledEpoch() const{return impl_->installedEpoch;}
// Match SingleMode: legacy refresh masks include reserved 0x100000; it has no query payload.
Bytes UndoDuel::QueryFieldBytes(int player,int location,unsigned int flags,int) {
    require(impl_->core!=nullptr,"Host query without core");
    return impl_->core->QueryField(static_cast<std::uint8_t>(player),static_cast<std::uint8_t>(location),flags & 0xefffffU);
}
Bytes UndoDuel::QueryCardBytes(int player,int location,int sequence,unsigned int flags) {
    require(impl_->core!=nullptr,"Host query without core");
    return impl_->core->QueryCard(static_cast<std::uint8_t>(player),static_cast<std::uint8_t>(location),static_cast<std::uint8_t>(sequence),flags & 0xefffffU);
}
} // namespace ygo
