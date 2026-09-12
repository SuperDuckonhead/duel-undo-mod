#pragma once

// RoomClient owns the epoch boundary; all display and side-editor packets still
// use the real native handler. Full connection teardown is covered by real pairs.
struct MatchClientFixture {
  SessionId session{NewSessionId()};
  uint64_t epoch{}, sequence{};
  std::vector<Envelope> sent;
  std::vector<Bytes> applied;
  unsigned automaticCount{};
  bool queueAutomatic{};
  RoomClient client;

  MatchClientFixture()
      : client(game, session,
               [&](const Envelope &e) { sent.push_back(Decode(Encode(e))); },
               [&](const Bytes &bytes) {
                 applied.push_back(bytes);
                 if (bytes[0] != STOC_DUEL_END)
                   DuelClient::HandleLegacySTOC(
                       const_cast<uint8_t *>(bytes.data()), bytes.size());
                 if (queueAutomatic && bytes.size() > 1 &&
                     bytes[0] == STOC_GAME_MSG && bytes[1] == MSG_SELECT_YESNO)
                   CHECK(!client.Submit(
                       {{1, 0, 0, 0}, Origin::Automatic, client.Token()}));
               },
               [&](const InputSubmission &input) {
                 ++automaticCount;
                 CHECK(client.Submit(input));
               }) {
    game.dInfo.isSingleMode = false;
    game.dInfo.isTag = false;
    game.dInfo.isReplay = false;
    game.dInfo.isFinished = false;
    game.is_siding = false;
    DuelClient::selftype = 0;
  }

  static Bytes start(uint8_t player = 0, uint32_t lp = 8000) {
    Bytes bytes{MSG_START, player, 4};
    put(bytes, lp, 4);
    put(bytes, lp, 4);
    for (unsigned i = 0; i < 4; ++i)
      put(bytes, 0, 2);
    return bytes;
  }
  static Bytes choice() {
    Bytes bytes{MSG_SELECT_YESNO, 0};
    put(bytes, 30, 4);
    return bytes;
  }
  void packet(Bytes bytes, uint64_t prompt = 0) {
    for (const auto &e :
         EncodeGamePacket(session, epoch, prompt, ++sequence, bytes))
      client.Receive(Decode(Encode(e)));
  }
  void frame(Bytes bytes, uint64_t prompt = 0) {
    bytes.insert(bytes.begin(), STOC_GAME_MSG);
    packet(std::move(bytes), prompt);
  }
  void status(uint64_t prompt, uint8_t player = 0, uint8_t eligible = 0) {
    RoomStatus value;
    value.prompt = prompt;
    value.promptPlayer = player;
    value.eligibleMask = eligible;
    value.clock.remainingMs = {{180000, 180000}};
    client.Receive({WireKind::Status, {session, epoch, 0, 0, {}},
                    EncodeRoomStatus(value)});
  }
  void firstPrompt() {
    frame(start());
    frame(choice(), 1);
    status(1, 0, 1);
    CHECK(!client.InputPaused() && client.CanUndo());
  }
  void win() { frame({MSG_WIN, 0, 0}, 1); }
  void next(uint8_t round) {
    client.Receive(EncodeRoundStart(session, epoch, epoch + 1, round));
    ++epoch;
    sequence = 0;
  }
  void restoreOne(const Bytes &initial, uint8_t player, uint64_t request,
                  bool resume = true) {
    const auto visible = BuildPlayerRestore(player, {initial}, choice());
    RoomRestore descriptor{1, 0, {{180000, 180000}}, EncodePlayerRestore(visible)};
    TxKey key{session, epoch, request, 0, {}};
    key.targetDigest[0] = 44;
    for (const auto &part : Fragment(EncodeRoomRestore(descriptor)))
      client.Receive({WireKind::Prepare, key, part});
    CHECK(!sent.empty() && sent.back().kind == WireKind::Ready &&
          sent.back().payload == Bytes{1});
    Bytes nextEpoch;
    put(nextEpoch, epoch + 1, 8);
    client.Receive({WireKind::Commit, key, nextEpoch});
    CHECK(sent.back().kind == WireKind::CommitAck);
    ++epoch;
    sequence = 0;
    if (resume)
      client.Receive({WireKind::Resume, key, nextEpoch});
    CHECK(client.InputPaused() == !resume && client.Token().epoch == epoch);
  }
};

static int runMatchClientLifecycleTests() {
  {
    MatchClientFixture f;
    f.firstPrompt();
    const auto stale = f.client.Token();
    CHECK(f.client.QueueLegacy({CTOS_TIME_CONFIRM}));
    CHECK(f.client.Submit({{0, 0, 0, 0}, Origin::Manual, stale}));
    TxKey consent{f.session, 0, 7, 0, {}};
    consent.targetDigest[0] = 17;
    f.client.Receive({WireKind::Consent, consent, {1}});
    CHECK(f.client.Consent(true, consent));
    f.win();
    CHECK(game.dInfo.isFinished && f.client.DuelEnded());
    CHECK(f.client.InputPaused() && !f.client.PresentationFrozen());
    CHECK(!f.client.CanUndo() && !f.client.NeedsConsent());
    CHECK(!f.client.RequestUndo() && !f.client.Consent(true, consent));
    CHECK(!f.client.QueueLegacy({CTOS_TIME_CONFIRM}));
    f.status(1, 0, 1);
    CHECK(f.client.InputPaused());
    f.packet({STOC_REPLAY});
    CHECK(f.applied.back() == Bytes{STOC_REPLAY});
    f.packet({STOC_CHANGE_SIDE});
    CHECK(game.is_siding && game.is_building && game.btnSideOK->isVisible());
    CHECK(!f.client.PresentationFrozen() && !f.client.RequestUndo());
    f.packet({STOC_WAITING_SIDE});
    CHECK(game.stHintMsg->isVisible());
    STOC_ErrorMsg error{};
    error.msg = ERRMSG_SIDEERROR;
    Bytes errorPacket(1 + sizeof(error));
    errorPacket[0] = STOC_ERROR_MSG;
    std::memcpy(errorPacket.data() + 1, &error, sizeof(error));
    f.packet(errorPacket);
    CHECK(f.applied.back() == errorPacket && game.btnSideOK->isVisible());
    f.packet({STOC_DUEL_START});
    CHECK(!game.is_building && !game.btnSideOK->isVisible());
    CHECK(game.dInfo.isStarted && !game.dInfo.isFinished);
    f.packet({STOC_SELECT_TP});
    CHECK(game.wFTSelect->isVisible());

    // Simulate a delayed old native response and presentation context. The
    // transition must clear both before the next MSG_START may reuse prompt 1.
    DuelPromptContext context;
    context.selectHint = 91;
    context.unselectHint = 92;
    context.lastHint = 93;
    context.event[0] = L'x';
    DuelClient::RestorePromptContext(context);
    DuelClient::SetResponseI(1);
    CHECK(!DuelClient::CaptureResponse().response.empty());
    f.next(2);
    CHECK(f.client.Token().session == f.session && f.client.Token().epoch == 1 &&
          f.client.Token().prompt == 0);
    CHECK(f.client.InputPaused() && !f.client.CanUndo() && !f.client.DuelEnded());
    CHECK(!f.client.QueueLegacy({CTOS_TIME_CONFIRM}));
    CHECK(DuelClient::CaptureResponse().response.empty());
    context = DuelClient::CapturePromptContext();
    CHECK(!context.selectHint && !context.unselectHint && !context.lastHint &&
          !context.event[0]);
    CHECK(game.fadingList.empty() && !game.wFTSelect->isVisible());
    f.client.Poll();
    CHECK(f.sent.empty());
    CHECK(!f.client.Submit({{0, 0, 0, 0}, Origin::Manual, stale}));

    // A repeated transition and every old control are inert in the new epoch.
    f.client.Receive(EncodeRoundStart(f.session, 0, 1, 2));
    for (auto kind : {WireKind::Status, WireKind::Prepare, WireKind::Commit,
                      WireKind::Resume, WireKind::Abort, WireKind::Consent,
                      WireKind::RequestRejected})
      f.client.Receive({kind, consent, {}});
    for (auto &e : EncodeGamePacket(f.session, 0, 1, f.sequence + 1,
                                    {STOC_GAME_MSG, MSG_WIN, 0, 0}))
      f.client.Receive(e);
    CHECK(!f.client.DuelEnded() && !f.client.NeedsConsent());
    f.frame(MatchClientFixture::start(1, 6000));
    f.frame(MatchClientFixture::choice(), 1);
    CHECK(f.client.InputPaused());
    f.status(1, 1);
    CHECK(!f.client.InputPaused() && !f.client.CanUndo());
    CHECK(game.dInfo.lp[0] == 6000 && game.dInfo.lp[1] == 6000);
    CHECK(f.client.QueueLegacy({CTOS_TIME_CONFIRM}));
    CHECK(f.client.Submit({{1, 0, 0, 0}, Origin::Manual, f.client.Token()}));
    f.client.Poll();
    CHECK(f.sent.size() == 2 && f.sent[0].kind == WireKind::Game &&
          f.sent[1].kind == WireKind::Response);
    GamePacketStream stream(f.session, 1);
    const auto confirm = stream.Add(f.sent[0]);
    CHECK(confirm && confirm->sequence == 1 && confirm->prompt == 1);
    CHECK(DecodeResponse(f.sent[1]).key.epoch == 1);

    f.win();
    f.packet({STOC_CHANGE_SIDE});
    f.packet({STOC_DUEL_START}); // Other seat does not receive SELECT_TP.
    f.next(3);
    CHECK(f.client.Token().epoch == 2 && f.client.Token().prompt == 0);
    f.frame(MatchClientFixture::start());
    f.frame(MatchClientFixture::choice(), 1);
    f.status(1);
    CHECK(!f.client.InputPaused() && !f.client.CanUndo());
    f.win();
    f.packet({STOC_DUEL_END});
    const auto applied = f.applied.size();
    f.packet({STOC_CHANGE_SIDE});
    f.packet({STOC_DUEL_START});
    f.client.Receive(EncodeRoundStart(f.session, 2, 3, 3));
    CHECK(f.client.DuelEnded() && f.client.InputPaused() &&
          f.client.Token().epoch == 2 && f.applied.size() == applied);
  }
  {
    MatchClientFixture f;
    f.firstPrompt();
    f.restoreOne(MatchClientFixture::start(), 0, 7);
    f.frame(MatchClientFixture::choice(), 2);
    f.status(2, 0, 1);
    CHECK(f.client.RequestUndo()); // Still queued when the authoritative win arrives.
    f.win();
    f.packet({STOC_CHANGE_SIDE});
    f.packet({STOC_DUEL_START});
    const auto count = f.sent.size();
    auto other = NewSessionId();
    f.client.Receive(EncodeRoundStart(other, 1, 2, 2));
    f.client.Receive(EncodeRoundStart(f.session, 0, 1, 2));
    CHECK(f.client.Token().epoch == 1 && f.client.InputPaused());
    f.next(2);
    CHECK(f.client.Token().epoch == 2);
    f.frame(MatchClientFixture::start(0, 6500));
    f.frame(MatchClientFixture::choice(), 1);
    f.status(1);
    f.client.Poll();
    CHECK(f.sent.size() == count && !f.client.CanUndo());
    // Same prompt number and recipient cannot authorize an old-game model.
    auto oldVisible = BuildPlayerRestore(0, {MatchClientFixture::start()},
                                         MatchClientFixture::choice());
    RoomRestore oldDescriptor{1, 0, {{180000, 180000}},
                              EncodePlayerRestore(oldVisible)};
    TxKey oldModel{f.session, 2, 1, 0, {}};
    oldModel.targetDigest[0] = 11;
    for (const auto &part : Fragment(EncodeRoomRestore(oldDescriptor)))
      f.client.Receive({WireKind::Prepare, oldModel, part});
    CHECK(f.sent.back().kind == WireKind::Ready && f.sent.back().payload == Bytes{0});
    f.client.Receive({WireKind::Abort, oldModel, {4, 1}});
    CHECK(!f.client.InputPaused() && game.dInfo.lp[0] == 6500);
    f.restoreOne(MatchClientFixture::start(0, 6500), 0, 2);
    CHECK(f.client.Token().epoch == 3);
    f.win();
    f.packet({STOC_CHANGE_SIDE});
    f.packet({STOC_DUEL_START});
    f.next(3);
    CHECK(f.client.Token().epoch == 4 && f.client.Token().prompt == 0);
  }
  {
    MatchClientFixture f;
    f.firstPrompt();
    f.queueAutomatic = true;
    f.frame(MatchClientFixture::choice(), 2);
    f.queueAutomatic = false;
    f.win();
    f.packet({STOC_CHANGE_SIDE});
    f.packet({STOC_DUEL_START});
    f.next(2);
    f.frame(MatchClientFixture::start());
    f.frame(MatchClientFixture::choice(), 1);
    f.status(1);
    f.client.Poll();
    CHECK(f.sent.empty() && f.automaticCount == 0 && !f.client.InputPaused());
  }
  // A valid codec is insufficient: authenticated round transitions must also
  // obey the native lifecycle. Bad current-epoch order permanently pauses it.
  for (unsigned scenario = 0; scenario < 7; ++scenario) {
    MatchClientFixture f;
    f.firstPrompt();
    if (scenario == 4) {
      RoomStatus failed;
      failed.state = TxState::PausedFailed;
      f.client.Receive({WireKind::Status, {f.session, 0, 0, 0, {}},
                        EncodeRoomStatus(failed)});
    }
    if (scenario != 0 && scenario != 6)
      f.win();
    if (scenario == 5)
      f.packet({STOC_DUEL_END});
    if (scenario >= 2)
      f.packet({STOC_CHANGE_SIDE});
    if (scenario >= 3 || scenario == 1)
      f.packet({STOC_DUEL_START});
    const auto before = f.applied.size();
    f.client.Receive(EncodeRoundStart(f.session, 0, 1, scenario == 3 ? 3 : 2));
    for (const auto &e : EncodeGamePacket(f.session, 1, 0, 1,
                                          {STOC_GAME_MSG, MSG_START, 0, 4}))
      f.client.Receive(e);
    CHECK(f.client.Token().epoch == 0 && f.client.InputPaused() &&
          f.applied.size() == before);
  }
  for (unsigned prematureFrame = 0; prematureFrame < 3; ++prematureFrame) {
    MatchClientFixture f;
    f.firstPrompt();
    f.win();
    f.packet({STOC_CHANGE_SIDE});
    f.packet({STOC_DUEL_START});
    f.next(2);
    const auto before = f.applied.size();
    if (prematureFrame == 1)
      f.frame(MatchClientFixture::choice(), 1);
    else if (prematureFrame == 2) {
      STOC_TimeLimit limit{};
      limit.player = 0;
      limit.left_time = 180;
      Bytes timer(1 + sizeof(limit));
      timer[0] = STOC_TIME_LIMIT;
      std::memcpy(timer.data() + 1, &limit, sizeof(limit));
      f.packet(timer, 1);
    }
    f.status(1);
    f.client.Poll();
    CHECK(f.client.InputPaused() && f.applied.size() == before);
    CHECK(f.sent.empty());
  }
  for (unsigned phase = 0; phase < 4; ++phase) {
    MatchClientFixture f;
    f.firstPrompt();
    f.win();
    if (phase >= 1)
      f.packet({STOC_CHANGE_SIDE});
    if (phase >= 2)
      f.packet({STOC_DUEL_START});
    if (phase == 3)
      f.next(2);
    RoomStatus failed;
    failed.state = TxState::PausedFailed;
    failed.prompt = phase == 3 ? 0 : 1;
    Envelope failure{WireKind::Status, {f.session, f.epoch, 0, 0, {}},
                     EncodeRoomStatus(failed)};
    if (phase != 3) {
      auto invalid = failure;
      ++invalid.key.epoch;
      f.client.Receive(invalid);
      invalid = failure;
      invalid.key.request = 1;
      f.client.Receive(invalid);
      invalid = failure;
      invalid.key.targetIndex = 1;
      f.client.Receive(invalid);
      invalid = failure;
      invalid.key.targetDigest[0] = 1;
      f.client.Receive(invalid);
      f.status(1, 0, 1);
      CHECK(f.client.InputPaused() && !f.client.PresentationFrozen());
    }
    f.client.Receive(failure);
    CHECK(f.client.InputPaused() && f.client.PresentationFrozen());
    CHECK(f.client.StatusText().find(L"已暂停对战") != std::wstring::npos);
    const auto before = f.applied.size();
    if (phase == 3)
      f.frame(MatchClientFixture::start());
    else
      f.client.Receive(EncodeRoundStart(f.session, f.epoch, f.epoch + 1, 2));
    CHECK(f.client.InputPaused() && f.client.Token().epoch == f.epoch &&
          f.applied.size() == before);
  }
  {
    MatchClientFixture f;
    f.firstPrompt();
    f.restoreOne(MatchClientFixture::start(), 0, 1, false);
    f.win();
    CHECK(game.dInfo.isFinished && f.client.DuelEnded() &&
          f.client.InputPaused() && !f.client.PresentationFrozen());
    f.packet({STOC_REPLAY});
    const auto before = f.applied.size();
    f.packet({STOC_CHANGE_SIDE});
    f.packet({STOC_DUEL_START});
    f.client.Receive(EncodeRoundStart(f.session, 1, 2, 2));
    CHECK(f.client.Token().epoch == 1 && f.client.InputPaused() &&
          f.applied.size() == before);
  }
  std::cout << "PASS actual RoomClient/native Match win, replay admission, "
               "siding correction, turn choice, round 2/3, stale inputs and "
               "permanent match end; undo/round epoch 0->1->2->3->4, fresh "
               "history, queued auto/request cleanup, invalid order gates and "
               "explicit failure before/after round transition; committed "
               "unconfirmed restore cannot reopen via Match\n";
  return 0;
}
