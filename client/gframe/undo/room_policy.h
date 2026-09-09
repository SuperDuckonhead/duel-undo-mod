#pragma once
#include <cstdint>
namespace undo {
// Uses the existing fixed STOC_ErrorMsg layout; ordinary room error codes stay unchanged.
constexpr std::uint8_t RoomPolicyError = 0x7e;
constexpr std::uint32_t RoomPolicyFatal = 0x80000000u;
enum class RoomPolicyReason : std::uint32_t {
    Tag = 1, Observer = 2, Full = 3, Incompatible = 4, MissingMod = 5, HandshakeTimeout = 6, Match = 7
};
inline std::uint32_t PolicyCode(RoomPolicyReason reason, bool fatal) {
    return static_cast<std::uint32_t>(reason) | (fatal ? RoomPolicyFatal : 0);
}
inline const wchar_t* PolicyMessage(std::uint32_t code) {
    switch(static_cast<RoomPolicyReason>(code & ~RoomPolicyFatal)) {
    case RoomPolicyReason::Tag: return L"撤回房间暂不支持 Tag 对战。";
    case RoomPolicyReason::Observer: return L"撤回房间仅支持两名对战玩家，暂不支持观战。";
    case RoomPolicyReason::Full: return L"撤回房间的两个对战席位已满，无法以观战者身份加入。";
    case RoomPolicyReason::Incompatible: return L"双方的撤回版本或决斗资源不一致，无法开始对战。";
    case RoomPolicyReason::MissingMod: return L"此房间需要双方使用兼容的撤回版客户端。";
    case RoomPolicyReason::HandshakeTimeout: return L"撤回版本确认超时，请重新加入房间。";
    case RoomPolicyReason::Match: return L"撤回版首版仅支持单局，暂不支持 Match（三局两胜与局间换副牌）。";
    default: return nullptr;
    }
}
}
