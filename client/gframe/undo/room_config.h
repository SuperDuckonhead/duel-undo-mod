#pragma once
#include "bot_controller.h"
#include "room_admission.h"
namespace ygo { class DataManager; }
namespace undo {
// Captured before starting the listener. All logical resources and AI selection
// inputs are immutable, shared with the actual host core and private AI worker.
struct RoomConfig {
    Hello capability;
    std::shared_ptr<const ResourceView> resources;
    std::wstring botExecutable;
    std::optional<BotLaunchData> bot;
};
std::shared_ptr<const RoomConfig> CaptureRoomConfig(ygo::DataManager&,
    const std::string& runtimeRoot, bool preferExpansionScript, RoomMode,
    const std::string& botSelection = {}, const std::string& customDeck = {});
} // namespace undo
