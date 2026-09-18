#pragma once
#include "bot_controller.h"
#include "room_admission.h"
#include <future>
namespace ygo { class DataManager; }
namespace undo {
struct TestDuelConfig;
// Host captures freeze executable resources and AI inputs before listening.
// Joining clients only carry the compatible protocol/card-data capability;
// resources and bot are empty and no local effect scripts are read.
struct RoomConfig {
    std::shared_ptr<const TestDuelConfig> deckTest;
    Hello capability;
    std::shared_ptr<const ResourceView> resources;
    std::wstring botExecutable;
    std::optional<BotLaunchData> bot;
};
std::shared_ptr<const RoomConfig> CaptureClientRoomConfig(ygo::DataManager&, RoomMode);
std::string ResolveDeckTestBotSelection(const Bytes& catalog);
std::shared_ptr<const RoomConfig> CaptureDeckTestRoomConfig(ygo::DataManager&,
    const std::string& runtimeRoot, bool preferExpansionScript,
    std::shared_ptr<const TestDuelConfig>);
// Copy the loaded card view and mounted archive metadata on the UI owner;
// only private archive readers and owned values cross into the worker.
std::future<std::shared_ptr<const RoomConfig>> PrepareDeckTestRoomConfig(ygo::DataManager&,
    const std::string& runtimeRoot, bool preferExpansionScript,
    std::shared_ptr<const TestDuelConfig>);
std::shared_ptr<const RoomConfig> CaptureRoomConfig(ygo::DataManager&,
    const std::string& runtimeRoot, bool preferExpansionScript, RoomMode,
    const std::string& botSelection = {}, const std::string& customDeck = {});
} // namespace undo
