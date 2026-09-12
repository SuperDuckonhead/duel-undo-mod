#pragma once
#include "protocol.h"
#include <stdexcept>

namespace undo {
// v1 deliberately admits one bounded main-deck birth. No host card identity,
// code, query, diagnostic, or introduction log is part of this recipient type.
struct TestStatePatch {
 uint8_t recipient{},owner{},controller{},sequence{};
 uint16_t expectedCount{};
};
inline void ValidateTestStatePatch(const TestStatePatch& p) {
 if(p.recipient>1 || p.owner>1 || p.controller!=p.owner || p.expectedCount>126 || p.sequence>p.expectedCount)
  throw std::runtime_error("Invalid TestStatePatch source");
}
inline Bytes EncodeTestStatePatch(const TestStatePatch& p) {
 ValidateTestStatePatch(p);
 return {1,0,0,0,0,0,0,0,p.recipient,p.owner,p.controller,1,p.sequence,8,uint8_t(p.expectedCount),uint8_t(p.expectedCount>>8)};
}
inline TestStatePatch DecodeTestStatePatch(const Bytes& b) {
 if(b.size()!=16 || b[0]!=1 || b[1] || b[2] || b[3] || b[4] || b[5] || b[6] || b[7] || b[11]!=1 || b[13]!=8)
  throw std::runtime_error("Malformed or unsupported TestStatePatch/v1");
 TestStatePatch p{b[8],b[9],b[10],b[12],uint16_t(b[14]|uint16_t(b[15])<<8)};ValidateTestStatePatch(p);return p;
}
struct PreparedContinuation {
 Bytes birth;
 std::vector<Bytes> messages; // recipient-filtered STOC_GAME_MSG only
};
inline void ValidateContinuationMessage(const Bytes& m,uint8_t recipient) {
 if(m.size()<2 || m.size()>65535 || m[0]!=1)throw std::runtime_error("Invalid recipient continuation frame");
 const auto opcode=m[1]; // fixed native selection opcodes; SELECT_SUM has a leading mode byte
 const bool prompt=(opcode>=10 && opcode<=26) || opcode==132 || (opcode>=140 && opcode<=143);
 const size_t seat=opcode==23?3:2;
 if(prompt && (m.size()<=seat || m[seat]!=recipient))throw std::runtime_error("Continuation contains another participant's selection");
}
inline Bytes EncodePreparedContinuation(const PreparedContinuation& c) {
 const auto patch=DecodeTestStatePatch(c.birth);
 if(c.messages.size()>4095)throw std::runtime_error("Too many continuation events");
 Bytes b{1,0,0,0};
 auto word=[&](uint32_t n){for(unsigned i=0;i<4;++i)b.push_back(uint8_t(n>>(8*i)));};
 word(uint32_t(c.messages.size()+1));b.push_back(1);word(uint32_t(c.birth.size()));b.insert(b.end(),c.birth.begin(),c.birth.end());
 for(const auto& m:c.messages) {
  ValidateContinuationMessage(m,patch.recipient);
  if(b.size()+5+m.size()>4*1024*1024)throw std::runtime_error("Continuation too large");
  b.push_back(0);word(uint32_t(m.size()));b.insert(b.end(),m.begin(),m.end());
 }
 return b;
}
inline PreparedContinuation DecodePreparedContinuation(const Bytes& b) {
 if(b.size()>4*1024*1024)throw std::runtime_error("Continuation too large");
 size_t at=0;auto word=[&](){if(b.size()-at<4)throw std::runtime_error("Truncated continuation");uint32_t n=0;for(unsigned i=0;i<4;++i)n|=uint32_t(b[at++])<<(8*i);return n;};
 if(word()!=1)throw std::runtime_error("Unsupported continuation version");
 auto count=word();if(!count || count>4096)throw std::runtime_error("Invalid continuation count");
 PreparedContinuation c;
 for(uint32_t i=0;i<count;++i) {
  if(at==b.size())throw std::runtime_error("Truncated continuation event");
  auto kind=b[at++];auto size=word();if(size>b.size()-at)throw std::runtime_error("Truncated continuation payload");
  Bytes input(b.begin()+at,b.begin()+at+size);at+=size;
  if(!i){if(kind!=1)throw std::runtime_error("Continuation must start with birth");DecodeTestStatePatch(input);c.birth=std::move(input);}
  else {if(kind!=0)throw std::runtime_error("Invalid or duplicate continuation event");ValidateContinuationMessage(input,DecodeTestStatePatch(c.birth).recipient);c.messages.push_back(std::move(input));}
 }
 if(at!=b.size())throw std::runtime_error("Trailing continuation bytes");return c;
}
}
