#include "core_fixture.h"
#include "data_manager.h"
#include "measurement_process.h"
#include "test_support.h"
#include "undo/host_bot_seat.h"
#include <IFileSystem.h>
#include <chrono>
#include <iostream>
#include <thread>
namespace irr {
namespace io {
IFileSystem *createFileSystem();
}
} // namespace irr
using namespace undo;
static BotCompletion wait(HostBotSeat &seat, std::uint64_t job) {
  auto end = std::chrono::steady_clock::now() + std::chrono::seconds(40);
  while (std::chrono::steady_clock::now() < end) {
    for (auto &c : seat.Poll())
      if (c.job == job)
        return c;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  throw std::runtime_error("HostBotSeat completion timeout");
}
static void suspend(DWORD pid) {
  measurement::Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0));
  THREADENTRY32 t{};
  t.dwSize = sizeof(t);
  CHECK(Thread32First(snapshot.get(), &t));
  unsigned count{};
  do {
    if (t.th32OwnerProcessID == pid) {
      measurement::Handle thread(
          OpenThread(THREAD_SUSPEND_RESUME, FALSE, t.th32ThreadID));
      CHECK(thread.get());
      CHECK(SuspendThread(thread.get()) != DWORD(-1));
      ++count;
    }
  } while (Thread32Next(snapshot.get(), &t));
  CHECK(count > 0);
}
int main(int argc, char **argv) {
  try {
    if (argc > 1 && std::string(argv[1]) == "--undo-control") {
      Sleep(30000);
      return 0;
    } // connection never arrives: cancellation fixture only
    CHECK(argc == 3);
    auto executable =
        std::filesystem::absolute(std::filesystem::u8path(argv[1])).wstring();
    std::string runtime = argv[2];
    std::unique_ptr<irr::io::IFileSystem, void (*)(irr::io::IFileSystem *)> fs(
        irr::io::createFileSystem(), [](auto *p) { p->drop(); });
    ygo::DataManager manager;
    manager.IrrFileSystem = fs.get();
    CHECK(manager.LoadDB(
        (std::filesystem::u8path(runtime).parent_path() / "cards.cdb")
            .u8string()
            .c_str()));
    auto root =
        (std::filesystem::current_path() / "host-bot-fixture").u8string();
    fixture::database(root);
    auto view = ResourceView::Capture(root, manager, false);
    BotLaunchData init;
    init.runtimeRoot = runtime;
    init.seed = 83;
    init.engine[0] = 1;
    init.resources = view->Fingerprint();
    init.cardView = CaptureBotCardView(*view, manager, init.engine);
    init.selectionCommand =
        "Name='Worker Seat' Deck=Lucky Dialog=gugugu.zh-CN Hand=3 Chat=false";
    init.hasCustomDeck = true;
    init.customDeckSource = "fixture:public.ydk";
    const std::string deck = "#main\n89631139\n46986414\n!side\n89631139\n";
    init.customDeck = Bytes(deck.begin(), deck.end());
    auto session = NewSessionId();
    auto base = measurement::CountDescendants();
    {
      HostBotSeat seat(executable, init, session, 7, 44);
      auto initialized = wait(seat, 1);
      CHECK(initialized.accepted &&
            initialized.identity.state == BotState::Running &&
            initialized.generation == 44);
      CHECK(initialized.selection.name == "Worker Seat" &&
            initialized.selection.hand == 3 &&
            initialized.selection.executor == "Lucky" &&
            initialized.selection.deckFile == "AI_Test");
      auto handshake = seat.Dispatch(7, 1, Bytes{0x12, 0, 0, 0, 0, 0, 0, 5});
      CHECK(handshake != 0);
      auto join = wait(seat, handshake);
      CHECK(join.outputs.size() == 1 && join.outputs[0].packet[0] == 2 &&
            join.outputs[0].packet[1] == 2);
      auto hand = seat.Dispatch(7, 2, Bytes{3});
      auto choice = wait(seat, hand);
      CHECK(choice.outputs.size() == 1 && choice.outputs[0].packet[1] == 3);
      auto old = choice.outputs[0];
      CHECK(AcceptsBotOutput(old, choice.identity, 2));
      const auto firstJob = seat.Dispatch(7, 3, Bytes{3});
      const auto secondJob = seat.Dispatch(7, 4, Bytes{3});
      const auto fenceJob = seat.Fence();
      std::vector<BotCompletion> ordered;
      auto deadline =
          std::chrono::steady_clock::now() + std::chrono::seconds(10);
      while ((ordered.empty() || ordered.back().job != fenceJob) &&
             std::chrono::steady_clock::now() < deadline) {
        auto batch = seat.Poll();
        for (auto &c : batch)
          ordered.push_back(std::move(c));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      CHECK(ordered.size() == 3 && ordered[0].job == firstJob &&
            ordered[1].job == secondJob && ordered[2].job == fenceJob);
      CHECK(ordered[0].outputs.at(0).prompt == 3 &&
            ordered[1].outputs.at(0).prompt == 4 && ordered[2].outputs.empty());
      auto fence = std::move(ordered.back());
      CHECK(fence.accepted && fence.cursor > initialized.cursor &&
            fence.job > hand);
      TxKey key{session, 7, 1, 0, {}};
      key.targetDigest[0] = 5;
      auto prepare = seat.Prepare(key, fence.cursor);
      CHECK(seat.Dispatch(7, 3, Bytes{3}) == 0);
      auto ready = wait(seat, prepare);
      CHECK(ready.accepted && ready.identity.state == BotState::Ready &&
            ready.candidatePid != ready.identity.activePid);
      CHECK(!AcceptsBotOutput(old, ready.identity, 2));
      auto commit = wait(seat, seat.Commit(key));
      CHECK(commit.accepted &&
            commit.retainedPid == initialized.identity.activePid &&
            commit.identity.epoch == 8);
      CHECK(!AcceptsBotOutput(old, commit.identity, 2));
      CHECK(seat.Dispatch(8, 3, Bytes{3}) == 0);
      auto duplicate = wait(seat, seat.Commit(key));
      CHECK(duplicate.accepted && duplicate.commitCount == 1);
      auto resumed = wait(seat, seat.Resume(key, 8));
      CHECK(resumed.accepted && resumed.retainedPid == 0);
      auto next = wait(seat, seat.Dispatch(8, 3, Bytes{3}));
      CHECK(next.outputs.size() == 1 && next.outputs[0].packet[1] == 3);
      CHECK(!AcceptsBotOutput(old, next.identity, 2));
      auto bad = key;
      bad.epoch = 8;
      bad.request = 2;
      auto rejected = wait(seat, seat.Prepare(bad, next.cursor + 1));
      CHECK(!rejected.accepted && rejected.identity.epoch == 8 &&
            rejected.identity.activePid == next.identity.activePid);
      const auto abortJob = seat.Abort(bad);
      CHECK(seat.Dispatch(8, 4, Bytes{3}) == 0);
      auto aborted = wait(seat, abortJob);
      CHECK(aborted.accepted && aborted.identity.state == BotState::Running);
      auto terminal = wait(seat, seat.Dispatch(8, 4, Bytes{1, 1}));
      CHECK(terminal.identity.state == BotState::Failed &&
            !terminal.failure.empty() && terminal.outputs.empty());
      seat.Stop();
    }
    CHECK(measurement::CountDescendants() == base);
    {
      HostBotSeat seat(executable, init, session, 7, 45);
      auto ready = wait(seat, 1);
      suspend(ready.identity.activePid);
      CHECK(seat.Dispatch(7, 1, Bytes{3}) != 0);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      auto start = std::chrono::steady_clock::now();
      seat.Stop();
      CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(6));
    }
    CHECK(measurement::CountDescendants() == base);
    {
      HostBotSeat seat(std::filesystem::absolute(argv[0]).wstring(), init,
                       session, 7, 46);
      std::this_thread::sleep_for(std::chrono::milliseconds(150));
      auto start = std::chrono::steady_clock::now();
      seat.Stop();
      CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(6));
    }
    CHECK(measurement::CountDescendants() == base);
    {
      auto branch = init;
      branch.selectionCommand = "Deck=ChainBurn Dialog=kiwi.zh-TW";
      branch.hasCustomDeck = false;
      branch.customDeck.clear();
      branch.customDeckSource.clear();
      HostBotSeat seat(executable, branch, session, 7, 47);
      CHECK(wait(seat, 1).accepted);
      Bytes start{1, 4, 0, 5};
      fixture::word(start, 8000);
      fixture::word(start, 8000);
      fixture::word(start, 56, 2);
      fixture::word(start, 3, 2);
      fixture::word(start, 40, 2);
      fixture::word(start, 0, 2);
      wait(seat, seat.Dispatch(7, 1, start));
      Bytes draw{1, 90, 0, 2};
      fixture::word(draw, 98645731);
      fixture::word(draw, 60990740);
      wait(seat, seat.Dispatch(7, 2, draw));
      Bytes activate{1, 11, 0, 0, 0, 0, 0, 0, 1};
      fixture::word(activate, 98645731);
      activate.insert(activate.end(), {0, 2, 0});
      fixture::word(activate, 0);
      activate.insert(activate.end(), {0, 1, 0});
      auto decision = wait(seat, seat.Dispatch(7, 3, activate));
      CHECK(decision.outputs.back().packet[1] == 5);
      auto fence = wait(seat, seat.Fence());
      TxKey key{session, 7, 1, 0, {}};
      key.targetDigest[0] = 9;
      CHECK(wait(seat, seat.Prepare(key, fence.cursor)).accepted);
      CHECK(wait(seat, seat.Commit(key)).accepted);
      CHECK(wait(seat, seat.Resume(key, 8)).accepted);
      Bytes summon{1, 11, 0, 1};
      fixture::word(summon, 60990740);
      summon.insert(summon.end(), {0, 2, 1, 0, 0, 0, 0, 0, 0, 1, 0});
      CHECK(wait(seat, seat.Dispatch(8, 4, summon)).outputs.back().packet[1] ==
            7);
      seat.Stop();
    }
    CHECK(measurement::CountDescendants() == base);
    {
      auto random = init;
      random.selectionCommand = "Random=AI_FIXED Hand=2 Chat=false";
      const std::string catalog = "!Frozen random\nDeck=Lucky\nfixture\nAI_FIXED\n";
      random.selectionCatalog = Bytes(catalog.begin(), catalog.end());
      HostBotSeat seat(executable, random, session, 7, 48);
      auto selected = wait(seat, 1);
      CHECK(selected.accepted && selected.selection.executor == "Lucky" &&
            selected.selection.deckFile == "AI_Test" &&
            selected.selection.dialog == "default");
      auto hand = wait(seat, seat.Dispatch(7, 1, Bytes{3}));
      CHECK(hand.outputs.at(0).packet[1] == 2);
      TxKey key{session, 7, 1, 0, {}};
      key.targetDigest[0] = 10;
      CHECK(wait(seat, seat.Prepare(key, hand.cursor)).accepted);
      CHECK(wait(seat, seat.Commit(key)).accepted);
      CHECK(wait(seat, seat.Resume(key, 8)).accepted);
      CHECK(wait(seat, seat.Dispatch(8, 2, Bytes{3})).outputs.at(0).packet[1] == 2);
      seat.Stop();
    }
    CHECK(measurement::CountDescendants() == base);
    {
      auto configured = init;
      configured.selectionCommand = "Config=missing-on-disk/bot.conf Name='CLI Seat' Hand=0x2";
      const std::string configText = "# captured\nDeck=Lucky\nName=Config Seat\nHand=1\nChat=false\nDialog=default\n";
      BotFrozenConfig config;
      config.source = "missing-on-disk/bot.conf";
      config.content = Bytes(configText.begin(), configText.end());
      config.sha256 = Sha256(config.content);
      configured.selectionConfigs = {config};
      const std::string appText = "<configuration><appSettings><add key='UsePreErrataEffects' value='true'/></appSettings></configuration>";
      config.source = "WindBot.exe.config";
      config.content = Bytes(appText.begin(), appText.end());
      config.sha256 = Sha256(config.content);
      configured.appSettings = config;
      HostBotSeat seat(executable, configured, session, 7, 49);
      configured.selectionConfigs[0].content[0] ^= 1;
      auto selected = wait(seat, 1);
      CHECK(selected.accepted && selected.selection.name == "CLI Seat" &&
            selected.selection.hand == 2 && !selected.selection.chat &&
            selected.selection.usePreErrataEffects);
      auto hand = wait(seat, seat.Dispatch(7, 1, Bytes{3}));
      CHECK(hand.outputs.at(0).packet[1] == 2);
      TxKey key{session, 7, 1, 0, {}};
      key.targetDigest[0] = 11;
      CHECK(wait(seat, seat.Prepare(key, hand.cursor)).accepted);
      CHECK(wait(seat, seat.Commit(key)).accepted);
      CHECK(wait(seat, seat.Resume(key, 8)).accepted);
      CHECK(wait(seat, seat.Dispatch(8, 2, Bytes{3})).outputs.at(0).packet[1] == 2);
      seat.Stop();
      const std::string catalog = "!One\nConfig='catalog config.conf' Deck=Lucky\nfixture\nAI_X\n!Two\nConfig=second.conf\nfixture\nAI_Y\n";
      auto sources = DiscoverBotConfigSources("Config='main config.conf' Random=AI_X", Bytes(catalog.begin(), catalog.end()));
      CHECK((sources == std::vector<std::string>{"catalog config.conf", "main config.conf", "second.conf"}));
    }
    CHECK(measurement::CountDescendants() == base);
    for(bool catalogHand : {false,true}) for(int menuHand : {0,1}) {
      auto selected = init;
      selected.selectionCommand = "Random=MENU_FIXED";
      const std::string catalog = std::string("!Menu\nConfig=menu.conf") + (catalogHand ? " Hand=3" : "") + "\nfixture\nMENU_FIXED\n";
      selected.selectionCatalog = Bytes(catalog.begin(),catalog.end());
      const std::string text = "Deck=Lucky\nName=Menu Seat\nHand=2\nChat=false\nDialog=default\n";
      selected.selectionConfigs = {{"menu.conf",Bytes(text.begin(),text.end()),{}}};
      selected.selectionConfigs[0].sha256 = Sha256(selected.selectionConfigs[0].content);
      selected.handOverride = menuHand;
      HostBotSeat seat(executable,selected,session,7,50+catalogHand*2+menuHand);
      selected.handOverride = 1-menuHand; // caller mutation cannot change the candidate
      auto result=wait(seat,1);
      CHECK(result.accepted && result.selection.hand==menuHand);
      auto cursor=result.cursor;
      auto original=wait(seat,seat.Dispatch(7,1,Bytes{3}));
      CHECK(original.outputs.size()==1);
      auto expected=original.outputs[0].packet;
      if(menuHand==1) CHECK(expected.at(1)==1);
      TxKey key{session,7,1,0,{}};key.targetDigest[0]=12;
      CHECK(wait(seat,seat.Prepare(key,cursor)).accepted);
      CHECK(wait(seat,seat.Commit(key)).accepted);
      CHECK(wait(seat,seat.Resume(key,8)).accepted);
      auto replayed=wait(seat,seat.Dispatch(8,1,Bytes{3}));
      CHECK(replayed.outputs.at(0).packet==expected);
      bool randomVaried=false;
      for(unsigned i=0;i<12;++i) {
        auto choice=wait(seat,seat.Dispatch(8,2+i,Bytes{3})).outputs.at(0).packet.at(1);
        CHECK(choice>=1 && choice<=3);
        if(menuHand==1) CHECK(choice==1);
        else if(choice!=expected.at(1)) randomVaried=true;
      }
      CHECK(menuHand==1 || randomVaried);
      seat.Stop();
    }
    CHECK(measurement::CountDescendants()==base);
    std::cout << "PASS actual Random/Config menu Hand unchecked0/checked1 and frozen candidate continuation\n";
    std::cout << "actual private-process ordered jobs/fence, fixed selection, "
                 "Prepare/Commit/Resume, terminal closure and blocked "
                 "read/connect Stop passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}