#include "room_wire.h"
#include <limits>
#include <stdexcept>
namespace undo {
namespace {
void require(bool value, const char* reason) {
    if(!value) throw std::invalid_argument(reason);
}
void put(Bytes& data, std::uint64_t value, unsigned width) {
    for(unsigned i=0;i<width;++i) data.push_back(static_cast<std::uint8_t>(value>>(8*i)));
}
std::uint64_t get(const Bytes& data, std::size_t offset, unsigned width) {
    require(offset<=data.size() && width<=data.size()-offset, "Truncated room data");
    std::uint64_t value{};
    for(unsigned i=0;i<width;++i) value|=std::uint64_t(data[offset+i])<<(8*i);
    return value;
}
void inputKey(const TxKey& key) {
    require(key.session!=SessionId{} && key.request && !key.targetIndex &&
            key.targetDigest==Digest{}, "Invalid response prompt identity");
}
void validate(const RoomStatus& status) {
    require(status.state>=TxState::Running && status.state<=TxState::PausedFailed &&
        status.eligibleMask<=3 && status.promptPlayer<=2 && status.timePlayer<=2 &&
        status.nextRequest && status.clock.remainingMs[0]>=0 &&
        status.clock.remainingMs[1]>=0, "Invalid public room status");
}
}
std::vector<Envelope> EncodeGamePacket(const SessionId& session, std::uint64_t epoch,
        std::uint64_t prompt, std::uint64_t sequence, const Bytes& packet) {
    require(session!=SessionId{} && sequence && !packet.empty() &&
            packet.size()<=MaxGamePacket, "Invalid game packet identity or size");
    const TxKey key{session,epoch,prompt,sequence,{}};
    std::vector<Envelope> result;
    for(auto& part:Fragment(packet)) result.push_back({WireKind::Game,key,std::move(part)});
    return result;
}
Envelope EncodeRoundStart(const SessionId& session, std::uint64_t previousEpoch,
        std::uint64_t nextEpoch, std::uint8_t number) {
    Bytes payload;
    put(payload,nextEpoch,8);payload.push_back(number);
    Envelope result{WireKind::RoundStart,{session,previousEpoch,0,0,{}},std::move(payload)};
    DecodeRoundStart(result);
    return result;
}
RoundTransition DecodeRoundStart(const Envelope& envelope) {
    require(envelope.kind==WireKind::RoundStart && envelope.key.session!=SessionId{} &&
        !envelope.key.request && !envelope.key.targetIndex && envelope.key.targetDigest==Digest{},
        "Invalid round transition identity");
    require(envelope.payload.size()==9, "Invalid round transition size");
    RoundTransition result{get(envelope.payload,0,8),envelope.payload[8]};
    require(envelope.key.epoch!=std::numeric_limits<std::uint64_t>::max() &&
        result.epoch==envelope.key.epoch+1 && result.number>=2 && result.number<=3,
        "Invalid round transition epoch or game number");
    return result;
}
GamePacketStream::GamePacketStream(SessionId session, std::uint64_t epoch) { Reset(session,epoch); }
void GamePacketStream::Reset(SessionId session, std::uint64_t epoch) {
    require(session!=SessionId{}, "Missing room session identity");
    session_=session;epoch_=epoch;lastSequence_=0;active_.reset();fragments_.reset();failed_=false;
}
std::optional<GamePacket> GamePacketStream::Add(const Envelope& envelope) {
    try {
        require(!failed_, "Room game stream failed");
        if(!IsCurrent(envelope.key,session_,epoch_)) return std::nullopt;
        require(envelope.kind==WireKind::Game && envelope.key.targetIndex &&
                envelope.key.targetDigest==Digest{}, "Invalid game envelope");
        if(envelope.key.targetIndex<=lastSequence_) return std::nullopt;
        require(lastSequence_!=std::numeric_limits<std::uint64_t>::max() &&
                envelope.key.targetIndex==lastSequence_+1, "Missing game packet");
        if(active_) require(SameKey(*active_,envelope.key), "Game fragment identity changed");
        else { active_=envelope.key;fragments_=std::make_unique<FragmentAssembler>(); }
        const auto index=get(envelope.payload,0,4), count=get(envelope.payload,4,4);
        const auto total=get(envelope.payload,8,4);
        require(total && total<=MaxGamePacket, "Game packet exceeds size limit");
        fragments_->Add(envelope.payload);
        if(index+1!=count) return std::nullopt;
        GamePacket result{fragments_->Finish(),envelope.key.request,envelope.key.targetIndex};
        lastSequence_=result.sequence;active_.reset();fragments_.reset();
        return result;
    } catch(...) { failed_=true;throw; }
}
Envelope EncodeResponse(const TxKey& key, Origin origin, const Bytes& response) {
    inputKey(key);
    require((origin==Origin::Manual || origin==Origin::Automatic) &&
            !response.empty() && response.size()<=256, "Invalid network response");
    Bytes payload{static_cast<std::uint8_t>(origin)};
    put(payload,response.size(),2);payload.insert(payload.end(),response.begin(),response.end());
    return {WireKind::Response,key,std::move(payload)};
}
NetworkResponse DecodeResponse(const Envelope& envelope) {
    require(envelope.kind==WireKind::Response, "Expected response envelope");
    inputKey(envelope.key);
    const auto origin=get(envelope.payload,0,1), length=get(envelope.payload,1,2);
    require(origin<=static_cast<unsigned>(Origin::Automatic) && length && length<=256 &&
            envelope.payload.size()==length+3, "Invalid network response");
    return {envelope.key,static_cast<Origin>(origin),
            Bytes(envelope.payload.begin()+3,envelope.payload.end())};
}
Bytes EncodeRoomStatus(const RoomStatus& status) {
    validate(status);
    Bytes result{static_cast<std::uint8_t>(status.state),status.eligibleMask,status.promptPlayer,status.timePlayer};
    put(result,status.nextRequest,8);put(result,status.prompt,8);
    for(auto clock:status.clock.remainingMs) put(result,static_cast<std::uint64_t>(clock),8);
    return result;
}
RoomStatus DecodeRoomStatus(const Bytes& bytes) {
    require(bytes.size()==36, "Invalid room status size");
    RoomStatus result;result.state=static_cast<TxState>(bytes[0]);result.eligibleMask=bytes[1];
    result.promptPlayer=bytes[2];result.timePlayer=bytes[3];
    result.nextRequest=get(bytes,4,8);result.prompt=get(bytes,12,8);
    for(unsigned i=0;i<2;++i) {
        const auto clock=get(bytes,20+8*i,8);
        require(clock<=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()),
                "Invalid room clock");
        result.clock.remainingMs[i]=static_cast<std::int64_t>(clock);
    }
    validate(result);return result;
}
} // namespace undo
