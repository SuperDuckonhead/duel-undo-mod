using System;
using System.IO;
using System.Linq;
using WindBot.Undo;
using YGOSharp.Network.Enums;
using YGOSharp.OCGWrapper.Enums;

class UndoTests
{
    static string runtime = Environment.GetEnvironmentVariable("WIND_BOT_RUNTIME") ?? @"F:\MyCardLibrary\ygopro\WindBot";
    static string database = Environment.GetEnvironmentVariable("WIND_BOT_DATABASE") ?? @"F:\MyCardLibrary\ygopro\cards.cdb";
    static void Check(bool condition, string message) { if (!condition) throw new InvalidOperationException(message); }
    static void Reject(Action action, string message)
    {
        bool rejected = false;
        try { action(); } catch (InvalidOperationException) { rejected = true; }
        Check(rejected, message);
    }
    static TapeEntry Entry(byte output)
    {
        return new TapeEntry { Kind = TapeKind.Decision, Call = "response", Input = new byte[] { 1 }, Output = new byte[] { output } };
    }
    static void TapeTests()
    {
        RuntimeTests.Run();
        var tape = new DecisionTape();
        tape.BeginReplay(new[] { Entry(2) });
        Reject(() => tape.Observe(Entry(3)), "AI divergence accepted");
        var original = Entry(2);
        tape = new DecisionTape(); tape.Observe(original); original.Input[0] = 9;
        var copy = tape.Snapshot(); copy[0].Output[0] = 8;
        Check(tape.Snapshot()[0].Input[0] == 1 && tape.Snapshot()[0].Output[0] == 2, "Tape aliases caller arrays");
        var expected = new[] { Entry(2) };
        tape = new DecisionTape(); tape.BeginReplay(expected); expected[0].Output[0] = 9;
        Reject(() => tape.RequireEnd(), "Missing event accepted");
        tape.Observe(Entry(2)); tape.RequireEnd();
        Reject(() => tape.Observe(Entry(2)), "Extra event accepted");
        tape = new DecisionTape(); tape.BeginReplay(new[] { Entry(2) });
        var reordered = Entry(2); reordered.Kind = TapeKind.Random;
        Reject(() => tape.Observe(reordered), "Wrong event kind accepted");
        var recorded = new DecisionTape(); var rng = new ReplayRandom(83, recorded); RandomCalls(rng);
        var replay = new DecisionTape(); replay.BeginReplay(recorded.Snapshot()); RandomCalls(new ReplayRandom(83, replay)); replay.RequireEnd();
        Check(recorded.Snapshot().Length == 5, "RNG double consumption");
        replay = new DecisionTape(); replay.BeginReplay(recorded.Snapshot());
        Reject(() => RandomCalls(new ReplayRandom(84, replay)), "Different RNG seed accepted");
        var sampled = new DecisionTape();
        var sampleRandom = new ReplayRandom(83, sampled);
        var sample = typeof(ReplayRandom).GetMethod("Sample", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
        sample.Invoke(sampleRandom, null);
        Check(sampled.Snapshot().Length == 1 && sampled.Snapshot()[0].Call == "Sample", "Protected Sample not recorded exactly once");
        byte[] captured = null;
        var connection = new YGOSharp.Network.YGOClient(bytes => captured = bytes);
        byte[] source = { 1, 2 }; connection.Send(source); source[1] = 3;
        Check(captured[1] == 2 && connection.NetworkSendCount == 0, "Sink output aliases source or sends network");
        Reject(() => connection.Connect(System.Net.IPAddress.Loopback, 1), "Offline client connected");
        Reject(() => connection.Initialize(null), "Offline client attached socket");
        Reject(() => new YGOSharp.Network.BinaryClient(new YGOSharp.Network.NetworkClient(), bytes => { }), "Mixed socket/sink ownership accepted");
        connection.Close();
        Console.WriteLine("PASS tape: divergence, kind/order, deep copies, missing/extra, every RNG override; sink blocks Connect/Initialize");
    }
    static void RandomCalls(Random rng) { rng.Next(); rng.Next(19); rng.Next(-8, 31); rng.NextDouble(); rng.NextBytes(new byte[17]); }
    static byte[] Packet(GameMessage message, Action<BinaryWriter> body)
    {
        using (var stream = new MemoryStream()) using (var w = new BinaryWriter(stream))
        { w.Write((byte)StocMessage.GameMsg); w.Write((byte)message); body(w); return stream.ToArray(); }
    }
    static BotInit Init(string executor)
    {
        string dialog = executor == "ChainBurn" ? "kiwi.zh-TW" : executor == "Dragun" ? "smart.zh-CN" : "soul.zh-CN";
        return BotInit.Capture(runtime, database, executor, "AI_" + executor, dialog, 31871, true);
    }
    static byte[] Start(string executor = "ChainBurn")
    {
        int main = 0, extra = 0; bool inExtra = false;
        foreach (string row in File.ReadAllLines(Path.Combine(runtime, "Decks", "AI_" + executor + ".ydk")))
        {
            string line = row.Trim();
            if (line == "!side") break;
            if (line == "#extra") { inExtra = true; continue; }
            int id; if (int.TryParse(line, out id)) { if (inExtra) extra++; else main++; }
        }
        return Packet(GameMessage.Start, w => { w.Write((byte)0); w.Write((byte)5); w.Write(8000); w.Write(8000); w.Write((short)main); w.Write((short)extra); w.Write((short)40); w.Write((short)0); });
    }
    static byte[] Draw(params int[] cards)
    {
        return Packet(GameMessage.Draw, w => { w.Write((byte)0); w.Write((byte)cards.Length); foreach (int c in cards) w.Write(c); });
    }
    static byte[] Effect(int id, byte sequence, CardLocation location = CardLocation.MonsterZone)
    {
        return Packet(GameMessage.SelectEffectYn, w => { w.Write((byte)0); w.Write(id); w.Write((byte)0); w.Write((byte)location); w.Write(sequence); w.Write((byte)0); w.Write(0); });
    }
    static byte[] Selection(params int[] cards)
    {
        return Packet(GameMessage.SelectCard, w => { w.Write((byte)0); w.Write((byte)0); w.Write((byte)1); w.Write((byte)1); w.Write((byte)cards.Length); for (int i = 0; i < cards.Length; i++) { w.Write(cards[i]); w.Write((byte)0); w.Write((byte)CardLocation.Deck); w.Write((byte)i); w.Write((byte)0); } });
    }
    static byte[] ActivatePot()
    {
        return Packet(GameMessage.SelectIdleCmd, w => { w.Write((byte)0); for (int i = 0; i < 5; i++) w.Write((byte)0); w.Write((byte)1); w.Write(98645731); w.Write((byte)0); w.Write((byte)CardLocation.Hand); w.Write((byte)0); w.Write(0); w.Write((byte)0); w.Write((byte)1); w.Write((byte)0); });
    }
    static byte[] SummonTourGuide()
    {
        return Packet(GameMessage.Move, w => { w.Write(10802915); w.Write((byte)0); w.Write((byte)CardLocation.Hand); w.Write((byte)0); w.Write((byte)0); w.Write((byte)0); w.Write((byte)CardLocation.MonsterZone); w.Write((byte)0); w.Write((byte)CardPosition.FaceUpAttack); w.Write(0); });
    }
    static byte[] SummonBackJack()
    {
        return Packet(GameMessage.SelectIdleCmd, w => { w.Write((byte)0); w.Write((byte)1); w.Write(60990740); w.Write((byte)0); w.Write((byte)CardLocation.Hand); w.Write((byte)1); for (int i = 0; i < 5; i++) w.Write((byte)0); w.Write((byte)0); w.Write((byte)1); w.Write((byte)0); });
    }
    static byte[] Response(byte[][] output) { return output.Last(p => p[0] == (byte)CtosMessage.Response); }
    static void EqualOutputs(byte[][] left, byte[][] right)
    {
        Check(left.Length == right.Length && left.Zip(right, (a, b) => a.SequenceEqual(b)).All(v => v), "Next callback outputs diverged");
    }
    static void RebuildTests()
    {
        using (var active = new ReplaySession(Init("ChainBurn")))
        {
            active.Dispatch(Start()); active.Dispatch(Draw(98645731, 60990740));
            Check(BitConverter.ToInt32(Response(active.Dispatch(ActivatePot())), 1) == 5, "Pot of Duality callback not accepted");
            var history = active.Snapshot(); var before = active.DecisionStateDigest();
            using (var candidate = new ReplaySession(Init("ChainBurn")))
            {
                candidate.Replay(history, history.Length);
                Check(active.ProcessId != candidate.ProcessId, "Candidate shares a process");
                Check(before.SequenceEqual(candidate.DecisionStateDigest()), "Persistent field/queue state digest differs");
                Check(before.SequenceEqual(active.DecisionStateDigest()), "Candidate changed active bot");
                var next = active.Dispatch(SummonBackJack()); EqualOutputs(next, candidate.Dispatch(SummonBackJack()));
                Check(BitConverter.ToInt32(Response(next), 1) == 7, "ChainBurn no_sp flag not retained");
                Check(active.NetworkSendCount == 0 && candidate.NetworkSendCount == 0, "Real network send occurred");
            }
            // The original process remains useful after destroying the candidate.
            Check(active.Dispatch(new byte[] { (byte)StocMessage.SelectHand }).Length == 1, "Original stopped");
        }
        using (var fresh = new ReplaySession(Init("ChainBurn")))
        {
            fresh.Dispatch(Start()); fresh.Dispatch(Draw(98645731, 60990740));
            Check(BitConverter.ToInt32(Response(fresh.Dispatch(SummonBackJack())), 1) == 0, "Persistent-state control is insensitive");
        }
        Console.WriteLine("PASS actual ChainBurnExecutor: PotOfDualityeff -> no_sp -> next summon refused; fresh control summons");
        using (var active = new ReplaySession(Init("Dragun")))
        {
            active.Dispatch(Start("Dragun")); active.Dispatch(Draw(10802915)); active.Dispatch(SummonTourGuide());
            active.Dispatch(Effect(10802915, 0)); // Actual TourGuide callback enqueues Sangan.
            var history = active.Snapshot();
            using (var candidate = new ReplaySession(Init("Dragun")))
            {
                candidate.Replay(history, history.Length);
                Check(active.DecisionStateDigest().SequenceEqual(candidate.DecisionStateDigest()), "Pending selector differs");
                var next = Selection(46986414, 26202165);
                var output = active.Dispatch(next); EqualOutputs(output, candidate.Dispatch(next));
                Check(Response(output).SequenceEqual(new byte[] { (byte)CtosMessage.Response, 1, 1 }), "Queued Sangan selection not consumed");
                Check(active.NetworkSendCount == 0 && candidate.NetworkSendCount == 0, "Real sends from selection/chat");
            }
        }
        Console.WriteLine("PASS actual DragunExecutor: TourGuideFromTheUnderworldEffect -> pending Sangan selector -> next SelectCard index 1");
        using (var active = new ReplaySession(Init("ChainBurn")))
        {
            active.Dispatch(Start()); active.Dispatch(new byte[] { (byte)StocMessage.SelectHand });
            var history = active.Snapshot();
            Check(history.Any(e => e.Kind == TapeKind.Random), "No real random callbacks observed");
            Check(history.Any(e => e.Kind == TapeKind.Decision && e.Output[0] == (byte)CtosMessage.Chat), "Chat RNG/output not captured");
            using (var candidate = new ReplaySession(Init("ChainBurn")))
            {
                candidate.Replay(history, history.Length);
                for (int i = 0; i < 8; i++) EqualOutputs(active.Dispatch(new byte[] { 3 }), candidate.Dispatch(new byte[] { 3 }));
                EqualOutputs(active.Dispatch(new byte[] { 0x17, 1, 2, 3 }), candidate.Dispatch(new byte[] { 0x17, 1, 2, 3 }));
                EqualOutputs(active.Dispatch(new byte[] { 0x16 }), candidate.Dispatch(new byte[] { 0x16 }));
                Check(active.NetworkSendCount == 0 && candidate.NetworkSendCount == 0, "Real send occurred");
            }
        }
        Console.WriteLine("PASS actual Executor RPS + dialog RNG: next eight responses match; chat/replay/end callbacks have zero network sends");
        using (var configured = new ReplaySession(Init("Monarch506")))
        {
            configured.Dispatch(Start("Monarch506"));
            using (var candidate = new ReplaySession(Init("Monarch506")))
            {
                var tape = configured.Snapshot(); candidate.Replay(tape, tape.Length);
                Check(configured.DecisionStateDigest().SequenceEqual(candidate.DecisionStateDigest()), "Config-dependent executor differs");
            }
        }
        Console.WriteLine("PASS actual Monarch506Executor constructor: fixed UsePreErrataEffects configuration");
        var quietInit = BotInit.Capture(runtime, database, "ChainBurn", "AI_ChainBurn", "kiwi.zh-TW", 31871, false);
        using (var active = new ReplaySession(quietInit))
        {
            active.Dispatch(Start()); var history = active.Snapshot();
            using (var candidate = new ReplaySession(quietInit))
            {
                candidate.Replay(history, history.Length);
                byte[] error = { (byte)StocMessage.ErrorMsg, 2, 0, 0, 0, 0, 0, 0, 0x60 };
                var output = active.Dispatch(error); EqualOutputs(output, candidate.Dispatch(error));
                Check(output.Length == 1 && output[0][0] == (byte)CtosMessage.Chat, "Forced deck-error chat changed with Chat=false");
                Check(active.NetworkSendCount == 0 && candidate.NetworkSendCount == 0, "Error path sent network data");
            }
        }
        Console.WriteLine("PASS actual deck-error callback: forced chat/RNG captured with Chat=false; zero network sends");
    }
    static void FaultTests()
    {
        var init = Init("ChainBurn"); TapeEntry[] history;
        using (var active = new ReplaySession(init))
        {
            active.Dispatch(Start()); active.Dispatch(new byte[] { 3 }); history = active.Snapshot();
            int decision = Array.FindIndex(history, e => e.Kind == TapeKind.Decision);
            using (var partial = new ReplaySession(init)) Reject(() => partial.Replay(history, decision), "Partial callback boundary accepted");
            var changed = history.Select(e => new TapeEntry { Kind = e.Kind, Call = e.Call, Input = (byte[])e.Input.Clone(), Output = (byte[])e.Output.Clone() }).ToArray();
            changed[decision].Output[changed[decision].Output.Length - 1] ^= 1;
            using (var candidate = new ReplaySession(init)) Reject(() => candidate.Replay(changed, changed.Length), "Changed real response accepted");
            int random = Array.FindIndex(history, e => e.Kind == TapeKind.Random);
            changed = history.Where((e, i) => i != random).ToArray();
            using (var candidate = new ReplaySession(init)) Reject(() => candidate.Replay(changed, changed.Length), "Missing random callback accepted");
            changed = history.Concat(new[] { new TapeEntry { Kind = TapeKind.Clock, Call = "unexpected", Input = new byte[0], Output = new byte[0] } }).ToArray();
            using (var candidate = new ReplaySession(init)) Reject(() => candidate.Replay(changed, changed.Length), "Undriven clock event accepted");
            int boundary = history.Length;
            var expectedNext = active.Dispatch(new byte[] { 3 });
            var extended = active.Snapshot();
            using (var candidate = new ReplaySession(init))
            {
                candidate.Replay(extended, boundary);
                EqualOutputs(expectedNext, candidate.Dispatch(new byte[] { 3 }));
                Check(candidate.DecisionStateDigest().SequenceEqual(active.DecisionStateDigest()), "Earlier boundary did not continue identically");
                Reject(() => candidate.Replay(history, history.Length), "Reused candidate accepted");
            }
            var before = active.DecisionStateDigest();
            using (var failed = new ReplaySession(init)) Reject(() => failed.Dispatch(new byte[] { 1, 4 }), "Malformed packet accepted");
            Check(before.SequenceEqual(active.DecisionStateDigest()), "Failed candidate altered active bot");
            Check(active.Dispatch(new byte[] { 3 }).Length == 1, "Active failed after rejected candidates");
        }
        var copyInit = Init("ChainBurn");
        using (var copied = new ReplaySession(copyInit))
        {
            copyInit.Executor = "Changed"; copyInit.Deck[0] ^= 1; copyInit.Options[0] ^= 1;
            copied.Dispatch(Start());
        }
        var differentOptions = BotInit.Capture(runtime, database, "ChainBurn", "AI_ChainBurn", "kiwi.zh-TW", 31871, true, true);
        using (var candidate = new ReplaySession(differentOptions)) Reject(() => candidate.Replay(history, history.Length), "Changed fixed options accepted");
        var differentSeed = BotInit.Capture(runtime, database, "ChainBurn", "AI_ChainBurn", "kiwi.zh-TW", 31872, true);
        using (var candidate = new ReplaySession(differentSeed)) Reject(() => candidate.Replay(history, history.Length), "Changed session seed accepted");
        init.ResourceDigest[0] ^= 1;
        using (var candidate = new ReplaySession(init)) Reject(() => candidate.Dispatch(Start()), "Changed resources accepted");
        Console.WriteLine("PASS faults: partial/earlier boundary, changed decision/RNG/options/seed/resources, malformed packet; active unaffected");
    }
    static int Main(string[] args)
    {
        try
        {
            Check(args.Length == 2 && args[0] == "--suite", "Usage: --suite tape|rebuild|faults|transactions|all");
            string suite = args[1]; Check(new[] { "tape", "rebuild", "faults", "transactions", "all" }.Contains(suite), "Unknown suite");
            if (suite == "tape" || suite == "all") TapeTests();
            if (suite == "rebuild" || suite == "all") RebuildTests();
            if (suite == "faults" || suite == "all") FaultTests();
            if (suite == "transactions" || suite == "all") TransactionTests.Run();
            return 0;
        }
        catch (Exception ex) { Console.Error.WriteLine(ex); return 1; }
    }
}
