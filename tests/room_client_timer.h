#pragma once

// The host sends the time limit before the selection message. A confirmation
// must retain that packet's prompt even when no earlier game frame published it.
static int timerConfirmationTests() {
  for (bool earlyPoll : {false, true}) {
    for (bool automatic : {false, true}) {
      const auto session = NewSessionId();
      std::vector<Envelope> sent;
      RoomClient *current = nullptr;
      bool autoReply = false;
      RoomClient client(
          game, session,
          [&](const Envelope &e) { sent.push_back(Decode(Encode(e))); },
          [&](const Bytes &packet) {
            // This is the legacy handler's transport callback. Keep the real
            // RoomClient admission/queue/serialization path under test.
            if (packet[0] == STOC_TIME_LIMIT) {
              CHECK(packet.size() == 1 + sizeof(STOC_TimeLimit));
              STOC_TimeLimit limit{};
              std::memcpy(&limit, packet.data() + 1, sizeof(limit));
              CHECK(limit.player == 0 && limit.left_time == 180);
              CHECK(current->QueueLegacy({CTOS_TIME_CONFIRM}));
            } else if (packet[0] == STOC_GAME_MSG) {
              game.always_chain = true;
              DuelClient::HandleLegacySTOC(
                  const_cast<uint8_t *>(packet.data()), packet.size());
              if (autoReply && packet[1] == MSG_SELECT_CHAIN)
                CHECK(!current->Submit({{255, 255, 255, 255}, Origin::Automatic,
                                        current->Token()}));
            }
          },
          [&](const InputSubmission &input) { CHECK(current->Submit(input)); });
      current = &client;
      uint64_t sequence = 0;
      auto packet = [&](Bytes bytes, uint64_t prompt) {
        for (const auto &e :
             EncodeGamePacket(session, 0, prompt, ++sequence, bytes))
          client.Receive(Decode(Encode(e)));
      };
      auto frame = [&](Bytes bytes, uint64_t prompt) {
        bytes.insert(bytes.begin(), STOC_GAME_MSG);
        packet(std::move(bytes), prompt);
      };
      auto status = [&](uint64_t prompt) {
        RoomStatus value;
        value.prompt = prompt;
        value.promptPlayer = 0;
        value.timePlayer = 0;
        value.clock.remainingMs = {{180000, 180000}};
        client.Receive({WireKind::Status, {session, 0, 0, 0, {}},
                        EncodeRoomStatus(value)});
      };
      STOC_TimeLimit limit{};
      limit.player = 0;
      limit.left_time = 180;
      Bytes timer(1 + sizeof(limit));
      timer[0] = STOC_TIME_LIMIT;
      std::memcpy(timer.data() + 1, &limit, sizeof(limit));
      Bytes start{MSG_START, 0, 4};
      put(start, 8000, 4);
      put(start, 8000, 4);
      for (int i = 0; i < 4; ++i)
        put(start, 0, 2);
      Bytes chain{MSG_SELECT_CHAIN, 0, 0, 0};
      put(chain, 0, 8);
      frame(start, 0);
      frame(chain, 1);
      status(1);
      const auto stale = client.Token();
      packet(timer, 2);
      if (earlyPoll)
        client.Poll();
      autoReply = automatic;
      frame(chain, 2);
      status(2);
      if (!automatic)
        CHECK(client.Submit(
            {{255, 255, 255, 255}, Origin::Manual, client.Token()}));
      client.Poll();
      CHECK(sent.size() == 2 && sent[0].kind == WireKind::Game &&
            sent[1].kind == WireKind::Response);
      GamePacketStream outbound(session, 0);
      auto confirmation = outbound.Add(sent[0]);
      CHECK(confirmation && confirmation->prompt == 2 &&
            confirmation->packet == Bytes{CTOS_TIME_CONFIRM});
      const auto response = DecodeResponse(sent[1]);
      CHECK(response.key.request == 2 &&
            response.origin == (automatic ? Origin::Automatic : Origin::Manual));
      CHECK(game.dInfo.time_left[0] == 180);
      CHECK(!client.Submit({{0, 0, 0, 0}, Origin::Manual, stale}));
      client.Poll();
      CHECK(sent.size() == 2);

      // Receiving a newer timer may bind its acknowledgement, but cannot
      // authorize a click on the still-visible old prompt.
      const auto oldPrompt = client.Token();
      packet(timer, 3);
      CHECK(client.InputPaused());
      CHECK(!client.Submit({{0, 0, 0, 0}, Origin::Manual, oldPrompt}));
      CHECK(!client.Submit({{0, 0, 0, 0}, Origin::Manual, client.Token()}));
      autoReply = false;
      frame(chain, 3);
      status(3);
      CHECK(client.Submit({{255, 255, 255, 255}, Origin::Manual,
                            client.Token()}));
      // Consent can arrive before the transport poll sends either the native
      // timer acknowledgement or the selection. Abort must resume both, once,
      // and in their original order so the host can accept the selection.
      TxKey consent{session, 0, 1, 0, {}};
      consent.targetDigest[0] = 1;
      client.Receive({WireKind::Consent, consent, {1}});
      client.Poll();
      CHECK(sent.size() == 2);
      client.Receive({WireKind::Abort, consent, {2, 1}});
      client.Poll();
      CHECK(sent.size() == 4 && sent[2].kind == WireKind::Game &&
            sent[3].kind == WireKind::Response);
      auto resumedConfirmation = outbound.Add(sent[2]);
      CHECK(resumedConfirmation && resumedConfirmation->prompt == 3 &&
            resumedConfirmation->packet == Bytes{CTOS_TIME_CONFIRM});
      CHECK(DecodeResponse(sent[3]).key.request == 3);
      client.Poll();
      CHECK(sent.size() == 4);
      for (bool wrongSession : {false, true}) {
        auto other = wrongSession ? NewSessionId() : session;
        for (const auto &e : EncodeGamePacket(other, wrongSession ? 0 : 1,
                                              4, sequence + 1, timer))
          client.Receive(e);
      }
      client.Poll();
      CHECK(sent.size() == 4);
      packet(timer, 4);
      client.Close();
      client.Poll();
      CHECK(sent.size() == 4);
    }
  }
  std::cout << "PASS 180-second time-before-chain confirmations, early/late "
               "poll, manual/automatic responses, queued confirmation/response "
               "surviving Consent/Abort, and stale/lifecycle gates\n";
  return 0;
}
