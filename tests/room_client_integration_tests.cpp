#include "client_card.h"
#include "data_manager.h"
#include "deck_manager.h"
#include "duelclient.h"
#include "game.h"
#include "netserver.h"
#include "replay.h"
#include "room_client.h"
#include "test_support.h"
#include "undo/player_restore.h"
#include "undo/room_config.h"
#include "undo/room_policy.h"
#include "undo/room_restore.h"
#include <algorithm>
#include <chrono>
#include <event2/thread.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <winsock2.h>
using namespace ygo;
using namespace undo;
static Game game;
class FailingFactory final : public irr::gui::IGUIElementFactory {
public:
  int remaining{-1}, calls{};
  irr::gui::IGUIElement *addGUIElement(const char *,
                                       irr::gui::IGUIElement *) override {
    if (remaining >= 0) {
      ++calls;
      if (remaining-- == 0)
        throw std::bad_alloc();
    }
    return nullptr;
  }
  irr::gui::IGUIElement *addGUIElement(irr::gui::EGUI_ELEMENT_TYPE,
                                       irr::gui::IGUIElement *) override {
    return nullptr;
  }
  irr::s32 getCreatableGUIElementTypeCount() const override { return 0; }
  irr::gui::EGUI_ELEMENT_TYPE
  getCreateableGUIElementType(irr::s32) const override {
    return irr::gui::EGUIET_ELEMENT;
  }
  const char *getCreateableGUIElementTypeName(irr::s32) const override {
    return nullptr;
  }
  const char *
  getCreateableGUIElementTypeName(irr::gui::EGUI_ELEMENT_TYPE) const override {
    return nullptr;
  }
};
static void put(Bytes &b, uint64_t n, unsigned width) {
  for (unsigned i = 0; i < width; ++i)
    b.push_back(uint8_t(n >> (8 * i)));
}
#include "room_client_ai.h"
#include "room_client_pair.h"
int main(int argc, char **argv) {
  try {
    mainGame = &game;
    CHECK(game.Initialize(std::filesystem::current_path()));
    game.frameSignal.SetNoWait(true);
    game.actionSignal.SetNoWait(true);
    if (argc == 3 && std::string(argv[1]) == "--ai")
      return aiGame();
    if (argc == 4 && std::string(argv[3]) == "--free")
      return pairGame(std::string(argv[1]) == "--pair-host", std::filesystem::u8path(argv[2]), true);
    if (argc == 3)
      return pairGame(std::string(argv[1]) == "--pair-host",
                      std::filesystem::u8path(argv[2]));
    if (std::getenv("N2_ROOM_POLICY_ONLY") ||
        std::getenv("N2_ROOM_MATCH_ONLY")) {
      std::cerr << "policy initialized" << std::endl;
      game.btnCreateHost->setEnabled(false);
      game.wLanWindow->setVisible(false);
      auto deliver = [&](RoomPolicyReason reason, bool fatal) {
        STOC_ErrorMsg error{};
        error.msg = RoomPolicyError;
        error.code = PolicyCode(reason, fatal);
        Bytes packet(1 + sizeof(error));
        packet[0] = STOC_ERROR_MSG;
        std::memcpy(packet.data() + 1, &error, sizeof(error));
        DuelClient::HandleLegacySTOC(packet.data(), packet.size());
      };
      std::function<bool(irr::gui::IGUIElement *, const wchar_t *)> contains =
          [&](irr::gui::IGUIElement *node, const wchar_t *text) {
            if (node->getText() && std::wstring(node->getText()) == text)
              return true;
            for (auto *child : node->getChildren())
              if (contains(child, text))
                return true;
            return false;
          };
      if (std::getenv("N2_ROOM_MATCH_ONLY")) {
        game.cbMatchMode->setSelected(1);
        irr::SEvent event{};
        event.EventType = irr::EET_GUI_EVENT;
        event.GUIEvent.Caller = game.btnHostConfirm;
        event.GUIEvent.EventType = irr::gui::EGET_BUTTON_CLICKED;
        game.menuHandler.OnEvent(event);
        CHECK(!NetServer::IsRunning() && !DuelClient::Room());
        CHECK(contains(game.env->getRootGUIElement(),
                       L"撤回房间暂不支持 Match 对战。"));
        std::cout << "PASS actual host menu rejects unsupported Match before "
                     "room start\n";
        return 0;
      }
      std::cerr << "policy deliver observer" << std::endl;
      deliver(RoomPolicyReason::Observer, false);
      std::cerr << "policy search observer" << std::endl;
      CHECK(contains(
          game.env->getRootGUIElement(),
          PolicyMessage(PolicyCode(RoomPolicyReason::Observer, false))));
      CHECK(!game.btnCreateHost->isEnabled() && !game.wLanWindow->isVisible());
      std::cerr << "policy deliver fatal" << std::endl;
      deliver(RoomPolicyReason::Incompatible, true);
      CHECK(contains(
          game.env->getRootGUIElement(),
          PolicyMessage(PolicyCode(RoomPolicyReason::Incompatible, true))));
      CHECK(game.btnCreateHost->isEnabled() && game.wLanWindow->isVisible());
      std::cout << "PASS actual Game bounded room policy errors and fatal-only "
                   "lobby recovery\n";
      return 0;
    }
    if (std::getenv("N2_ROOM_CLOSE_ONLY")) {
      auto owner = std::make_shared<RoomClient>(
          game, NewSessionId(), [](const Envelope &) {}, [](const Bytes &) {},
          [](const InputSubmission &) {});
      auto ui = owner;
      std::thread transport([&] {
        owner->Close();
        owner.reset();
      });
      transport.join();
      CHECK(ui->InputPaused());
      {
        std::lock_guard<std::mutex> lock(game.gMutex);
        ui.reset();
      }
      std::cout << "PASS transport disposal before final UI-held reference "
                   "under Game mutex\n";
      return 0;
    }
    if (std::getenv("N2_ROOM_CAPTURE_ONLY")) {
      auto write = [](const char *path, const char *text) {
        std::ofstream file(path, std::ios::binary);
        file << text;
      };
      write("WindBot/n2-selection.conf",
            "Name = Frozen config name\nHand=2\nDeck=ChainBurn\n");
      write("WindBot/WindBot.exe.config",
            "<configuration><appSettings "
            "configSource=\"n2-settings.xml\"/></configuration>");
      write("WindBot/n2-settings.xml",
            "<appSettings file=\"n2-extra.xml\"><add key=\"Chat\" "
            "value=\"false\"/><add key=\"Name\" value=\"App "
            "name\"/></appSettings>");
      write(
          "WindBot/n2-extra.xml",
          "<appSettings><add key=\"Dialog\" value=\"default\"/></appSettings>");
      write("deck/n2-public.ydk", "#main\n70368879\n70368879\n#extra\n!side\n");
      auto config = CaptureRoomConfig(
          dataManager, std::filesystem::current_path().u8string(), false,
          RoomMode::ConsentLan, "Config=n2-selection.conf",
          "deck/n2-public.ydk");
      CHECK(config->bot && config->bot->hasCustomDeck);
      CHECK(config->capability.engine != Digest{} &&
            config->capability.rules != Digest{} &&
            config->capability.resources == config->resources->Fingerprint());
      CHECK(config->bot->selectionConfigs.size() == 1);
      CHECK(config->bot->appSettings);
      CHECK(config->bot->appSettings->source.find("n2-extra.xml") !=
            std::string::npos);
      auto fixed = config->bot->customDeck;
      write("WindBot/n2-selection.conf", "Name=CHANGED\nDeck=INVALID\n");
      write(
          "WindBot/n2-extra.xml",
          "<appSettings><add key=\"Dialog\" value=\"missing\"/></appSettings>");
      write("deck/n2-public.ydk", "#main\n0\n");
      BotController bot(config->botExecutable, *config->bot, NewSessionId(), 0);
      CHECK(bot.Selection().name == "Frozen config name" &&
            bot.Selection().hand == 2 &&
            bot.Selection().executor == "ChainBurn" &&
            bot.Selection().dialog == "default" && !bot.Selection().chat);
      CHECK(config->bot->customDeck == fixed);
      std::cout << "PASS actual room capture + private worker frozen Config, "
                   "XML indirections, custom deck and merged resources\n";
      return 0;
    }
    if (std::getenv("N2_ROOM_HANDSHAKE_ONLY")) {
      WSADATA winsock{};
      CHECK(WSAStartup(MAKEWORD(2, 2), &winsock) == 0);
      evthread_use_windows_threads();
      auto config = CaptureRoomConfig(
          dataManager, std::filesystem::current_path().u8string(), false,
          RoomMode::LoopbackFree);
      unsigned short port = 0;
      CHECK(NetServer::StartServer(0, 0x7f000001, &port, false,
                                   &config->capability, config));
      DuelClient::ConfigureRoom(config);
      CHECK(DuelClient::StartClient(0x7f000001, port, true));
      bool joined = false;
      for (int i = 0; i < 2500; ++i) {
        game.device->run();
        {
          std::lock_guard<std::mutex> lock(game.gMutex);
          game.DrawGUI();
          joined = bool(DuelClient::Room()) && game.wHostPrepare->isVisible();
        }
        if (joined)
          break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
      if (!joined) {
        std::cerr << "room=" << bool(DuelClient::Room())
                  << " prepare=" << game.wHostPrepare->isVisible()
                  << " server=" << NetServer::IsRunning()
                  << " fades=" << game.fadingList.size()
                  << " message=" << game.wMessage->isVisible() << "\n";
      }
      CHECK(joined);
      CHECK(DuelClient::Room()->Token().session != SessionId{});
      DuelClient::StopClient();
      NetServer::StopServer();
      for (int i = 0; i < 2500 && DuelClient::Room(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      CHECK(!DuelClient::Room());
      std::cout << "PASS actual Game/DuelClient/NetServer TCP capability and "
                   "lobby initialization\n";
      return 0;
    }
    auto session = NewSessionId();
    std::vector<Envelope> sent;
    RoomClient *current = nullptr;
    bool sendAutomatic = true;
    auto *factory = new FailingFactory;
    game.env->registerGUIElementFactory(factory);
    factory->drop();
    RoomClient client(
        game, session,
        [&](const Envelope &e) { sent.push_back(Decode(Encode(e))); },
        [&](const Bytes &b) {
          DuelClient::HandleLegacySTOC(const_cast<uint8_t *>(b.data()),
                                       b.size());
          if (sendAutomatic && b.size() > 1 && b[1] == MSG_SELECT_YESNO) {
            sendAutomatic = false;
            CHECK(!current->Submit(
                {{1, 0, 0, 0}, Origin::Automatic, current->Token()}));
          }
        },
        [&](const InputSubmission &i) {
          bool acquired = false;
          std::thread observer([&] {
            acquired = game.gMutex.try_lock();
            if (acquired)
              game.gMutex.unlock();
          });
          observer.join();
          CHECK(!acquired);
          CHECK(client.Submit(i));
        });
    current = &client;
    Bytes start{MSG_START, 0, 4};
    put(start, 8000, 4);
    put(start, 8000, 4);
    for (int i = 0; i < 4; ++i)
      put(start, 0, 2);
    Bytes prompt{MSG_SELECT_YESNO, 0};
    put(prompt, 30, 4);
    uint64_t seq = 0;
    auto gameFrame = [&](Bytes b, uint64_t id) {
      b.insert(b.begin(), STOC_GAME_MSG);
      for (auto &e : EncodeGamePacket(session, 0, id, ++seq, b))
        client.Receive(Decode(Encode(e)));
    };
    gameFrame(start, 0);
    gameFrame(prompt, 1);
    CHECK(client.InputPaused());
    if (std::getenv("N2_ROOM_TIME_CONFIRM")) {
      CHECK(client.QueueLegacy({CTOS_TIME_CONFIRM}));
      client.Poll();
      CHECK(sent.size() == 1 && sent.back().kind == WireKind::Game);
      GamePacketStream inputStream(session, 0);
      auto packet = inputStream.Add(sent.back());
      CHECK(packet && packet->prompt == 1 &&
            packet->packet == Bytes{CTOS_TIME_CONFIRM});
      std::cout << "PASS current-prompt time confirmation during pristine "
                   "capture pause\n";
      return 0;
    }
    InputSubmission automatic{{1, 0, 0, 0}, Origin::Automatic, client.Token()};
    CHECK(!client.Submit(automatic));
    CHECK(sent.empty());
    RoomStatus status;
    status.prompt = 1;
    status.promptPlayer = 0;
    status.eligibleMask = 1;
    status.clock = {{12000, 15000}};
    client.Receive(
        {WireKind::Status, {session, 0, 0, 0, {}}, EncodeRoomStatus(status)});
    client.Poll();
    CHECK(sent.size() == 1);
    CHECK(sent.back().kind == WireKind::Response);
    CHECK(DecodeResponse(sent.back()).origin == Origin::Automatic);
    if (std::getenv("N2_ROOM_RESPONSE_GATE") || std::getenv("N2_ROOM_REMOTE_CONSENT") ||
        std::getenv("N2_ROOM_PENDING_REQUEST") ||
        std::getenv("N2_ROOM_BUSY_REVIEW")) {
      gameFrame(prompt, 2);
      status.prompt = 2;
      client.Receive(
          {WireKind::Status, {session, 0, 0, 0, {}}, EncodeRoomStatus(status)});
    }
    if(std::getenv("N2_ROOM_REMOTE_CONSENT")) {
      CHECK(client.Submit({{0,0,0,0},Origin::Manual,client.Token()}));
      TxKey other{session,0,5,0,{}};other.targetDigest[0]=3;
      client.Receive({WireKind::Consent,other,{1}});
      auto count=sent.size();client.Poll();CHECK(sent.size()==count);
      client.Receive({WireKind::Abort,other,{0,1}});
      client.Poll();CHECK(sent.size()==count+1 && sent.back().kind==WireKind::Response);
      client.Poll();CHECK(sent.size()==count+1);
      std::cout << "PASS unsent response survives another consent then abort without duplicate delivery\n";return 0;
    }
    if (std::getenv("N2_ROOM_RESPONSE_GATE")) {
      CHECK(client.CanUndo());
      auto token = client.Token();
      CHECK(client.Submit({{0, 0, 0, 0}, Origin::Manual, token}));
      CHECK(!client.CanUndo() && !client.RequestUndo() &&
            !client.InputPaused());
      auto count = sent.size();
      client.Poll();
      CHECK(sent.size() == count + 1 &&
            sent.back().kind == WireKind::Response &&
            sent.back().key.request == 2);
      client.Poll();
      CHECK(sent.size() == count + 1);
      client.Receive(
          {WireKind::Status, {session, 0, 0, 0, {}}, EncodeRoomStatus(status)});
      CHECK(!client.CanUndo() && !client.RequestUndo());
      gameFrame(prompt, 3);
      status.prompt = 3;
      client.Receive(
          {WireKind::Status, {session, 0, 0, 0, {}}, EncodeRoomStatus(status)});
      CHECK(client.CanUndo());
      CHECK(client.Submit({{0, 0, 0, 0}, Origin::Manual, client.Token()}));
      client.Poll();
      gameFrame({MSG_RETRY}, 3);
      client.Receive(
          {WireKind::Status, {session, 0, 0, 0, {}}, EncodeRoomStatus(status)});
      CHECK(client.CanUndo() && !client.InputPaused());
      std::cout << "PASS pending/sent Manual response excludes undo until new "
                   "prompt or Retry boundary\n";
      return 0;
    }
    if (std::getenv("N2_ROOM_PENDING_REQUEST")) {
      auto *widget = game.wQuery;
      CHECK(client.RequestUndo());
      client.Receive(
          {WireKind::Status, {session, 0, 0, 0, {}}, EncodeRoomStatus(status)});
      CHECK(client.InputPaused());
      CHECK(!client.Submit({{0, 0, 0, 0}, Origin::Manual, client.Token()}));
      CHECK(game.wQuery == widget);
      client.Poll();
      CHECK(sent.back().kind == WireKind::Request);
      client.Receive(
          {WireKind::Abort, {session, 0, status.nextRequest, 0, {}}, {0, 1}});
      CHECK(!client.InputPaused() && game.wQuery == widget);
      std::cout << "PASS queued request remains paused through same-prompt "
                   "Running status\n";
      return 0;
    }
    if (std::getenv("N2_ROOM_CONSENT_REVIEW") ||
        std::getenv("N2_ROOM_CONSENT_TERMINAL")) {
      TxKey consentKey{session, 0, 2, 17, {}};
      consentKey.targetDigest[0] = 8;
      client.Receive({WireKind::Consent, consentKey, {1}});
      CHECK(client.NeedsConsent());
      CHECK(client.StatusText().find(L"18") != std::wstring::npos &&
            client.StatusText().find(L"玩家2") != std::wstring::npos);
      if (std::getenv("N2_ROOM_CONSENT_TERMINAL")) {
        gameFrame({MSG_WIN, 0, 0}, 1);
        CHECK(game.dInfo.isFinished && !client.NeedsConsent() &&
              client.InputPaused());
        std::cout << "PASS terminal WIN removes stale consent while keeping "
                     "gameplay closed\n";
        return 0;
      }
      status.state = TxState::PausedFailed;
      client.Receive(
          {WireKind::Status, {session, 0, 0, 0, {}}, EncodeRoomStatus(status)});
      CHECK(!client.NeedsConsent());
      std::cout << "PASS public requester/operation consent and "
                   "failed-transaction consent cleanup\n";
      return 0;
    }
    if (std::getenv("N2_ROOM_BUSY_REVIEW")) {
      const TxKey own{session, 0, status.nextRequest, 0, {}};
      CHECK(client.RequestUndo());
      client.Poll();
      CHECK(sent.back().kind == WireKind::Request);
      TxKey other{session, 0, status.nextRequest + 1, 4, {}};
      other.targetDigest[0] = 9;
      client.Receive({WireKind::Consent, other, {1}});
      CHECK(client.NeedsConsent() && client.InputPaused());
      auto wrongTarget = own;
      wrongTarget.targetIndex = 1;
      client.Receive({WireKind::RequestRejected, wrongTarget, {1}});
      CHECK(client.NeedsConsent() && client.InputPaused());
      wrongTarget = own;
      wrongTarget.targetDigest[0] = 1;
      client.Receive({WireKind::RequestRejected, wrongTarget, {1}});
      CHECK(client.NeedsConsent() && client.InputPaused());
      auto mismatch = own;
      ++mismatch.request;
      client.Receive({WireKind::RequestRejected, mismatch, {1}});
      CHECK(client.NeedsConsent() && client.InputPaused());
      client.Receive({WireKind::RequestRejected, own, {1}});
      CHECK(client.NeedsConsent() && client.InputPaused());
      auto busy = client.StatusText();
      CHECK(busy.find(L"已有") != std::wstring::npos);
      client.Receive(
          {WireKind::Status, {session, 0, 0, 0, {}}, EncodeRoomStatus(status)});
      CHECK(client.StatusText().find(L"已有") != std::wstring::npos);
      client.Receive({WireKind::RequestRejected, own, {1}});
      CHECK(client.NeedsConsent() && client.InputPaused());
      auto stale = own;
      ++stale.epoch;
      client.Receive({WireKind::RequestRejected, stale, {2}});
      CHECK(client.NeedsConsent() && client.InputPaused());
      client.Receive({WireKind::Abort, other, {0, 1}});
      CHECK(!client.InputPaused());
      status.nextRequest += 2;
      client.Receive(
          {WireKind::Status, {session, 0, 0, 0, {}}, EncodeRoomStatus(status)});
      CHECK(client.RequestUndo());
      auto third = other;
      third.request = status.nextRequest + 1;
      client.Receive({WireKind::Consent, third, {1}});
      auto count = sent.size();
      client.Poll();
      CHECK(sent.size() == count && client.NeedsConsent() &&
            client.InputPaused());
      CHECK(client.StatusText().find(L"已有") != std::wstring::npos);
      client.Receive({WireKind::Abort, third, {0, 1}});
      CHECK(!client.InputPaused());
      std::cout << "PASS competing request rejection/order, sticky busy and "
                   "stale rejection isolation\n";
      return 0;
    }
    auto visible = BuildPlayerRestore(0, {start}, prompt);
    TxKey key{session, 0, 1, 0, {}};
    key.targetDigest[0] = 23;
    RoomRestore restore{1, 0, {{12000, 15000}}, EncodePlayerRestore(visible)};
    // Reach a later visible prompt, then fail the real widget factory after N3
    // accepted/projected the historical field. Original controls and clock
    // survive.
    Bytes lp{MSG_LPUPDATE, 0};
    put(lp, 7000, 4);
    gameFrame(lp, 2);
    if (std::getenv("N2_ROOM_AUTO_REPEAT"))
      sendAutomatic = true;
    gameFrame(prompt, 2);
    status.prompt = 2;
    status.nextRequest = 1;
    client.Receive(
        {WireKind::Status, {session, 0, 0, 0, {}}, EncodeRoomStatus(status)});
    if (std::getenv("N2_ROOM_AUTO_REPEAT")) {
      client.Poll();
      CHECK(sent.size() == 2 && sent.back().key.request == 2);
      std::cout << "PASS consecutive pristine automatic prompt delivery\n";
      return 0;
    }
    auto originalToken = client.Token();
    auto *originalQuery = game.wQuery;
    game.env->setFocus(game.btnYes);
    auto *originalFocus = game.env->getFocus();
    const auto originalLP = game.dInfo.lp[0];
    const auto originalClock = game.dInfo.time_left[0];
    factory->remaining = 1;
    for (auto &part : Fragment(EncodeRoomRestore(restore)))
      client.Receive({WireKind::Prepare, key, part});
    CHECK(factory->calls == 2);
    CHECK(sent.back().kind == WireKind::Ready &&
          sent.back().payload == Bytes{0});
    CHECK(game.wQuery == originalQuery &&
          game.env->getFocus() == originalFocus &&
          game.dInfo.lp[0] == originalLP &&
          game.dInfo.time_left[0] == originalClock &&
          client.Token() == originalToken);
    CHECK(client.InputPaused());
    client.Receive({WireKind::Abort, key, {4, 0}});
    CHECK(sent.back().kind == WireKind::AbortAck);
    CHECK(client.InputPaused());
    client.Receive({WireKind::Abort, key, {4, 1}});
    CHECK(!client.InputPaused());
    CHECK(client.Submit({{0, 0, 0, 0}, Origin::Manual, originalToken}));
    client.Poll();
    CHECK(DecodeResponse(sent.back()).key.request == 2);
    factory->remaining = -1;
    key.request = 2;
    for (auto &part : Fragment(EncodeRoomRestore(restore)))
      client.Receive({WireKind::Prepare, key, part});
    CHECK(sent.back().kind == WireKind::Ready &&
          sent.back().payload == Bytes{1});
    CHECK(client.InputPaused());
    CHECK(client.PresentationFrozen());
    auto *old = game.wQuery;
    auto stale = key;
    stale.request--;
    Bytes badEpoch;
    put(badEpoch, 1, 8);
    auto before = sent.size();
    client.Receive({WireKind::Commit, stale, badEpoch});
    CHECK(sent.size() == before && game.wQuery == old);
    Bytes epoch;
    put(epoch, 1, 8);
    client.Receive({WireKind::Commit, key, epoch});
    CHECK(sent.back().kind == WireKind::CommitAck);
    CHECK(game.wQuery != old);
    CHECK(client.InputPaused());
    CHECK(!client.Submit(automatic));
    auto committedCount = sent.size();
    client.Receive({WireKind::Commit, key, epoch});
    CHECK(sent.size() == committedCount + 1 &&
          sent.back().kind == WireKind::CommitAck && game.wQuery != old);
    client.Receive({WireKind::Resume, key, epoch});
    CHECK(!client.InputPaused());
    auto input = automatic;
    input.token = client.Token();
    input.origin = Origin::Manual;
    input.response = Bytes(256, 7);
    CHECK(client.Submit(input));
    client.Poll();
    CHECK(DecodeResponse(sent.back()).response.size() == 256);
    auto n = sent.size();
    client.Receive({WireKind::Resume, key, epoch});
    CHECK(sent.size() == n);
    // Old epoch messages cannot repaint the installed prompt.
    gameFrame(lp, 2);
    CHECK(game.dInfo.lp[0] == 8000);
    auto invalid = input;
    invalid.origin = Origin::Bot;
    CHECK(!client.Submit(invalid));
    invalid = input;
    invalid.response.resize(257);
    CHECK(!client.Submit(invalid));
    status.state = TxState::PausedFailed;
    client.Receive(
        {WireKind::Status, {session, 1, 0, 0, {}}, EncodeRoomStatus(status)});
    CHECK(client.InputPaused());
    game.dInfo.isFinished = false;
    for (auto &e : EncodeGamePacket(session, 1, 1, 1,
                                    Bytes{STOC_GAME_MSG, MSG_WIN, 0, 0}))
      client.Receive(e);
    CHECK(game.dInfo.isFinished);
    CHECK(client.InputPaused());
    CHECK(!client.Submit(input));
    std::cout
        << "PASS actual Game room client allocation rollback, abort barrier, "
           "commit/resume, stale tokens, full 256-byte transport\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
