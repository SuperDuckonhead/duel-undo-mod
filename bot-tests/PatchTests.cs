using System;
using System.IO;
using System.Linq;
using System.Reflection;
using WindBot.Game;
using WindBot.Undo;

internal static class PatchTests
{
    static void Check(bool value, string message) { if (!value) throw new InvalidOperationException(message); }
    static byte[] Bytes(Action<BinaryWriter> write)
    { using (var stream = new MemoryStream()) using (var w = new BinaryWriter(stream)) { write(w); return stream.ToArray(); } }
    internal static byte[] Birth(byte recipient, byte source, ushort count, byte sequence)
    { return Bytes(w => { w.Write(1u); w.Write(0u); w.Write(new[] { recipient, source, source, (byte)1, sequence, (byte)8 }); w.Write(count); }); }
    internal static byte[] Extension(byte[] birth, params byte[][] messages)
    { return Bytes(w => { w.Write(1u); w.Write((uint)(1 + messages.Length)); w.Write((byte)1); w.Write(birth.Length); w.Write(birth); foreach (var m in messages) { w.Write((byte)0); w.Write(m.Length); w.Write(m); } }); }
    static byte[] Start(byte recipient)
    { return Bytes(w => { w.Write(new byte[] { 1, 4, recipient, 5 }); w.Write(8000); w.Write(8000); w.Write((ushort)1); w.Write((ushort)0); w.Write((ushort)1); w.Write((ushort)0); }); }
    static byte[] Move(byte source, byte sequence)
    { return Bytes(w => { w.Write(new byte[] { 1, 50 }); w.Write(89631139); w.Write(new byte[] { source, 1, sequence, 8, source, 32, 0, 1 }); w.Write(64); }); }
    static object Field(object value, string name) { return value.GetType().GetField(name, BindingFlags.NonPublic | BindingFlags.Instance).GetValue(value); }
    static object Call(object value, string name, params object[] args)
    { return value.GetType().GetMethod(name, BindingFlags.NonPublic | BindingFlags.Instance).Invoke(value, args); }
    internal static void Run(BotInit original)
    {
        // Reflection is test-only: the production state/protocol paths run unchanged.
        // Resource binding includes the fixed deck, so use the selected-init factory.
        var init = BotInit.CaptureSelected(original.RuntimeRootForTests(), new BotSelection {
            Executor = "ChainBurn", DeckFile = "AI_ChainBurn", Dialog = "kiwi.zh-TW", Chat = false,
            CustomDeckSource = "fixture:source.ydk", CustomDeck = System.Text.Encoding.UTF8.GetBytes("#main\n89631139\n#extra\n!side\n")
        }, original.Seed, original.MergedCards, original.EngineDigest, original.CoreResourceDigest);
        byte[] sid = new byte[16]; sid[0] = 66;
        using (var control = new UndoControl(init, sid, 0))
        {
            control.Dispatch(sid, 0, 1, Start(0));
            var active = (ReplaySession)Field(control, "active");
            ulong cursor = control.Cursor; byte[] before = active.DecisionStateDigest(); uint pid = control.ActivePid;
            Func<ulong, BotTxKey> key = request => { var value = new BotTxKey { Session = sid, Request = request }; value.TargetDigest[0] = 1; return value; };
            byte[] extension = Extension(Birth(0, 1, 1, 1), Move(1, 1));
            Check(!control.Prepare(key(1), cursor, extension), "Unnegotiated extension accepted");
            Check(!control.NegotiateTestStatePatch(2) && control.NegotiateTestStatePatch(1), "Version capability refusal failed");
            ulong requestId = 1;
            var version = (byte[])extension.Clone(); version[0] = 2;
            var ordinal = (byte[])extension.Clone(); ordinal[17] = 1;
            var reordered = (byte[])extension.Clone(); reordered[8] = 0;
            var duplicate = (byte[])extension.Clone(); duplicate[29] = 1;
            var wrongCount = Extension(Birth(0, 1, 2, 2));
            var wrongSeat = Extension(Birth(1, 1, 1, 1));
            var foreignPrompt = Extension(Birth(0, 1, 1, 1), new byte[] { 1, 13, 1, 0, 0, 0, 0 });
            foreach (var invalid in new[] { version, ordinal, reordered, duplicate, wrongCount, wrongSeat, foreignPrompt, extension.Take(15).ToArray(), extension.Concat(new byte[] { 0 }).ToArray() })
            {
                var transaction = key(requestId++);
                Check(!control.Prepare(transaction, cursor, invalid), "Malformed/duplicate/out-of-order patch accepted");
                control.Abort(transaction);
                Check(control.State == BotUndoState.Running && control.ActivePid == pid && active.DecisionStateDigest().SequenceEqual(before), "Rejected patch changed active AI/card/tape state");
            }
            var abort = key(requestId++);
            Check(control.Prepare(abort, cursor, extension), "Valid extended prepare failed: " + control.Failure);
            var candidate = (ReplaySession)Field(control, "candidate");
            Check(candidate.Snapshot().Count(e => e.Kind == TapeKind.TestStatePatch) == 1 && candidate.Cursor > cursor, "Candidate patch/input tape missing");
            Check(active.DecisionStateDigest().SequenceEqual(before), "Prepare published candidate early");
            Check(control.Prepare(abort, cursor, (byte[])extension.Clone()) && !control.Prepare(abort, cursor, version), "Prepare idempotency did not bind extension bytes");
            control.Abort(abort);
            Check(control.ActivePid == pid && active.DecisionStateDigest().SequenceEqual(before), "Abort changed original state");
            var commit = key(requestId++);
            Check(control.Prepare(commit, cursor, extension), "Second preparation failed");
            candidate = (ReplaySession)Field(control, "candidate"); ulong preparedCursor = candidate.Cursor;
            Check(control.Commit(commit) && control.Commit(commit), "Extended commit failed");
            Check(control.Cursor == preparedCursor && control.RetainedPid == pid && control.CommitCount == 1, "Install lost candidate tape cursor or retained process");
            Check(control.Dispatch(sid, 1, 2, new byte[] { 3 }).Length == 0, "Committed candidate emitted before Resume");
            Check(control.Resume(commit, 1), "Extended resume failed");
            active = (ReplaySession)Field(control, "active"); before = active.DecisionStateDigest();
            var ordinary = key(requestId++); ordinary.Epoch = 1;
            Check(control.Prepare(ordinary, control.Cursor), "Ordinary restore cannot replay a stored patch");
            candidate = (ReplaySession)Field(control, "candidate");
            Check(candidate.DecisionStateDigest().SequenceEqual(before), "Stored patch replay changed recipient state/tape");
            control.Abort(ordinary);
            Check(control.Dispatch(sid, 1, 3, new byte[] { 3 }).Length == 1, "Committed bot could not continue normally");
        }
        Console.WriteLine("PASS actual patch prepare negotiation, immutable idempotency, malformed/order refusal, abort, commit/Resume and later ordinary replay");
        foreach (byte recipient in new byte[] { 0, 1 })
        {
            var tape = new DecisionTape();
            var type = typeof(ReplaySession).Assembly.GetType("WindBot.Undo.ReplayWorker", true);
            var worker = Activator.CreateInstance(type, BindingFlags.NonPublic | BindingFlags.Instance, null, new object[] { init, tape }, null);
            Call(worker, "Dispatch", Start(recipient));
            var game = (GameClient)Field(worker, "game");
            var duel = (Duel)Field(Field(game, "_behavior"), "_duel");
            var field = duel.Fields[0];
            var old = field.Deck[0];
            Call(worker, "ApplyPatch", Birth(recipient, recipient, 1, 1));
            var born = field.Deck[1];
            Check(field.Deck.Count == 2 && born.Id == 0 && born.Data == null && born.Owner == 0 && born.Controller == 0,
                "New source must be an unknown recipient-local deck slot");
            Check(ReferenceEquals(old, field.Deck[0]) && old.Sequence == 0, "Existing deck order changed");
            Check(field.GetCardCountInDeck(89631139) == 1, "Birth leaked a secret code into .ydk accounting");
            byte[] reveal = Bytes(w => { w.Write(new byte[] { 1, 7, recipient, 1, 1 }); w.Write(12); w.Write(1); w.Write(89631139); });
            Call(worker, "Dispatch", reveal);
            Check(field.GetCardCountInDeck(89631139) == 2, "Normally revealed introduced source not reflected in known deck count");
            Call(worker, "Dispatch", Move(recipient, 1));
            Check(ReferenceEquals(born, field.Banished[0]) && born.Id == 89631139 && ReferenceEquals(old, field.Deck[0]),
                "Ordinary reveal did not move the same source object");
            Check(field.GetCardCountInDeck(89631139) == 1, "Introduced duplicate consumed an unrelated original .ydk copy");
        }
        Console.WriteLine("PASS recipient localization, unknown birth/order, same-object reveal and separate own-deck accounting");
    }
    // Keep test setup's private option parsing out of production APIs.
    static string RuntimeRootForTests(this BotInit init)
    { return Environment.GetEnvironmentVariable("WIND_BOT_RUNTIME") ?? @"F:\MyCardLibrary\ygopro\WindBot"; }
    static byte[] ReadBlob(BinaryReader reader)
    {
        int count = reader.ReadInt32(); Check(count >= 0 && count <= 32 * 1024 * 1024, "Invalid native fixture blob");
        byte[] bytes = reader.ReadBytes(count); Check(bytes.Length == count, "Truncated native fixture"); return bytes;
    }
    static byte[][] ReadMessages(BinaryReader reader)
    {
        int count = reader.ReadInt32(); Check(count >= 0 && count <= 4096, "Invalid native fixture message count");
        var messages = new byte[count][]; for (int i = 0; i < count; ++i) messages[i] = ReadBlob(reader); return messages;
    }
    internal static void Native(string directory)
    {
        foreach (string name in new[] { "Gold", "Fusion" })
        using (var reader = new BinaryReader(File.OpenRead(Path.Combine(directory, name + ".bin"))))
        {
            Check(reader.ReadInt32() == 1, "Native fixture version"); byte[] cards = ReadBlob(reader);
            foreach (byte recipient in new byte[] { 0, 1 })
            {
                byte[] deck = ReadBlob(reader); byte[][] prefix = ReadMessages(reader); byte[] birth = ReadBlob(reader); byte[][] suffix = ReadMessages(reader);
                var init = BotInit.CaptureSelected((Environment.GetEnvironmentVariable("WIND_BOT_RUNTIME") ?? @"F:\MyCardLibrary\ygopro\WindBot"),
                    new BotSelection { Executor = "ChainBurn", DeckFile = "AI_ChainBurn", Dialog = "kiwi.zh-TW", Chat = false,
                        CustomDeckSource = "fixture:original-native-prefix.ydk", CustomDeck = deck }, 31871,
                    cards, cards.Skip(4).Take(32).ToArray(), cards.Skip(36).Take(32).ToArray());
                var type = typeof(ReplaySession).Assembly.GetType("WindBot.Undo.ReplayWorker", true);
                var worker = Activator.CreateInstance(type, BindingFlags.NonPublic | BindingFlags.Instance, null, new object[] { init, new DecisionTape() }, null);
                foreach (var packet in prefix) Call(worker, "Dispatch", packet);
                var game = (GameClient)Field(worker, "game"); var duel = (Duel)Field(Field(game, "_behavior"), "_duel");
                var field = duel.Fields[duel.GetLocalPlayer(birth[10])]; var before = field.Deck.ToArray();
                int sequence = birth[12]; Check(field.Deck.Count == BitConverter.ToUInt16(birth, 14), "Native source birth boundary count mismatch");
                Call(worker, "ApplyPatch", birth); var born = field.Deck[sequence];
                Check(born.Id == 0 && born.Data == null && !before.Contains(born), "Birth leaked source identity or reused an old card");
                Check(field.Deck.Count == before.Length + 1, "Native birth did not add one slot");
                for (int i = 0; i < before.Length; ++i) Check(ReferenceEquals(before[i], field.Deck[i < sequence ? i : i + 1]), "Native birth reordered original objects");
                foreach (var packet in suffix) Call(worker, "Dispatch", packet);
                var destination = name == "Gold" ? field.Banished : field.Graveyard;
                Check(destination.Contains(born) && born.Id == 89631139 && field.Deck.Count == 1,
                    "Original native move did not reveal the same introduced object with the right remaining count");
                Check(field.Deck.Any(c => before.Contains(c)) && !field.Deck.Contains(born), "Original duplicate was consumed instead of introduced source");
                if (recipient == 0) Check(field.GetCardCountInDeck(89631139) == 1, "Original native movement corrupted .ydk duplicate count");
                using (var active = new ReplaySession(init))
                {
                    foreach (var packet in prefix) active.Dispatch(packet);
                    var tape = active.Snapshot(); var digest = active.DecisionStateDigest();
                    using (var candidate = new ReplaySession(init))
                    {
                        candidate.Replay(tape, tape.Length); candidate.ApplyTestStatePatch(birth);
                        foreach (var packet in suffix) candidate.Dispatch(packet);
                        Check(active.DecisionStateDigest().SequenceEqual(digest), "Native prepared source changed active worker");
                        var installed = candidate.Snapshot(); Check(installed.Count(e => e.Kind == TapeKind.TestStatePatch) == 1, "Native birth tape entry missing or duplicated");
                        Check(installed.Single(e => e.Kind == TapeKind.TestStatePatch).Input.SequenceEqual(birth), "Host-private data entered patch tape");
                        using (var restored = new ReplaySession(init))
                        {
                            restored.Replay(installed, installed.Length);
                            Check(restored.DecisionStateDigest().SequenceEqual(candidate.DecisionStateDigest()), "Native stored patch replay changed state/AI/tape");
                        }
                        Check(candidate.NetworkSendCount == 0 && active.NetworkSendCount == 0, "Native preparation sent network data");
                    }
                }
                foreach (IDisposable lease in (System.Collections.IEnumerable)Field(worker, "resourceLeases")) lease.Dispose();
                Console.WriteLine("PASS original " + name + " native filtered packets, recipient " + recipient + ": unknown birth/order, same ClientCard reveal, counts, real isolated replay and later patch replay");
            }
            Check(reader.BaseStream.Position == reader.BaseStream.Length, "Trailing native fixture bytes");
        }
    }
}
