#pragma once
#include "bot_controller.h"
#include "room_admission.h"
namespace ygo { class DataManager; }
namespace undo {
// Host captures freeze executable resources and AI inputs before listening.
// Joining clients only carry the compatible protocol/card-data capability;
// resources and bot are empty and no local effect scripts are read.
struct RoomConfig {
    Hello capability;
    std::shared_ptr<const ResourceView> resources;
    std::wstring botExecutable;
    std::optional<BotLaunchData> bot;
};
std::shared_ptr<const RoomConfig> CaptureClientRoomConfig(ygo::DataManager&, RoomMode);
std::shared_ptr<const RoomConfig> CaptureRoomConfig(ygo::DataManager&,
    const std::string& runtimeRoot, bool preferExpansionScript, RoomMode,
    const std::string& botSelection = {}, const std::string& customDeck = {});
} // namespace undo
