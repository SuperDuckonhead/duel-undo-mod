using System;
using System.IO;
using System.Linq;
using System.Diagnostics;
using System.Reflection;
using System.IO.Pipes;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Text;
using WindBot.Undo;
using YGOSharp.OCGWrapper;

internal static class TransactionTests
{
    static void Check(bool value, string message) { if (!value) throw new InvalidOperationException(message); }
    static byte[] Fixture(string method, params object[] args) { return (byte[])typeof(UndoTests).GetMethod(method, BindingFlags.Static | BindingFlags.NonPublic).Invoke(null, args); }
    static byte[] Session() { var id = new byte[16]; id[0] = 91; return id; }
    static BotTxKey Key(ulong epoch = 7, ulong request = 1) { var key = new BotTxKey { Session = Session(), Epoch = epoch, Request = request, TargetIndex = 0 }; key.TargetDigest[0] = 44; return key; }
    static bool Alive(uint pid) { try { using (var p = Process.GetProcessById((int)pid)) return !p.HasExited; } catch (ArgumentException) { return false; } }
    static ReplaySession Active(UndoControl control) { return (ReplaySession)typeof(UndoControl).GetField("active", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(control); }
    static FrozenCard[] ReadOriginal()
    {
        string db = Environment.GetEnvironmentVariable("WIND_BOT_DATABASE") ?? @"F:\MyCardLibrary\ygopro\cards.cdb";
        NamedCardsManager.Init(db, true);
        return NamedCardsManager.GetAllCards().Select(c =>
        {
            var value = new FrozenCard { Code = (uint)c.Id, Alias = (uint)c.Alias, Type = (uint)c.Type, Level = (uint)c.Level, Race = (uint)c.Race, Attribute = (uint)c.Attribute, Attack = c.Attack, Defense = c.Defense, LScale = (uint)c.LScale, RScale = (uint)c.RScale, LinkMarker = (uint)c.LinkMarker, Ot = (uint)c.Ot, Name = c.Name, Text = c.Description };
            for (int i = 0; i < 4; i++) value.Setcodes[i] = (ushort)((ulong)c.Setcode >> (i * 16));
            return value;
        }).ToArray();
    }
    static BotInit Init(string executor, FrozenCard[] cards)
    {
        byte[] engine = new byte[32], resources = new byte[32]; engine[0] = 1; resources[0] = 2;
        var bytes = FrozenCardView.Encode(engine, resources, cards);
        string runtime = Environment.GetEnvironmentVariable("WIND_BOT_RUNTIME") ?? @"F:\MyCardLibrary\ygopro\WindBot";
        return BotInit.CaptureMerged(runtime, executor, "AI_" + executor, executor == "Dragun" ? "smart.zh-CN" : "kiwi.zh-TW", 31871, true, false, bytes, engine, resources);
    }
    static BotOutput[] Send(UndoControl control, ulong prompt, byte[] message) { return control.Dispatch(Session(), control.Epoch, prompt, message); }
    static int Decision(BotOutput[] outputs) { return BitConverter.ToInt32(outputs.Last(o => o.Packet[0] == 1).Packet, 1); }
    static void PrivateChannelTests()
    {
        Type type = typeof(ReplaySession).Assembly.GetType("WindBot.Undo.PrivatePipe");
        string name = (string)type.GetMethod("NewName", BindingFlags.Static | BindingFlags.NonPublic).Invoke(null, null);
        using (var server = (NamedPipeServerStream)type.GetMethod("Server", BindingFlags.Static | BindingFlags.NonPublic).Invoke(null, new object[] { name }))
        {
            var security = server.GetAccessControl();
            Check(security.AreAccessRulesProtected, "Pipe inherited broad ACL");
            var rules = security.GetAccessRules(true, true, typeof(SecurityIdentifier)).Cast<PipeAccessRule>().ToArray();
            Check(rules.Count(r => r.AccessControlType == AccessControlType.Allow) == 1 && rules.Single(r => r.AccessControlType == AccessControlType.Allow).IdentityReference.Equals(WindowsIdentity.GetCurrent().User), "Pipe grants noncreator user access");
            Check(rules.Any(r => r.AccessControlType == AccessControlType.Deny && r.IdentityReference.Equals(new SecurityIdentifier(WellKnownSidType.NetworkSid, null))), "Pipe permits remote network token");
            var connected = server.BeginWaitForConnection(null, null);
            using (var client = new NamedPipeClientStream(".", name, PipeDirection.InOut))
            {
                client.Connect(1000); server.EndWaitForConnection(connected); connected.AsyncWaitHandle.Dispose();
                bool rejected = false;
                try { type.GetMethod("VerifyClient", BindingFlags.Static | BindingFlags.NonPublic).Invoke(null, new object[] { server, -1 }); }
                catch (TargetInvocationException ex) { rejected = ex.InnerException is InvalidOperationException; }
                Check(rejected, "Unexpected same-user client PID was accepted");
            }
        }
        Console.WriteLine("PASS W2 actual named pipe ACL: protected current-user allow, network deny, wrong client PID rejected");
    }
    internal static void Run()
    {
        Check(!UndoControl.CanEmitResponse(BotUndoState.Frozen, 7, 7), "Frozen participant emitted");
        PrivateChannelTests();
        var cards = ReadOriginal();
        // This mimics already-resolved DataManager expansion values; the native
        // fixture separately constructs the bridge from a real merged DataManager.
        var expanded = cards.Single(c => c.Code == 26202165); expanded.Attack = 2468; expanded.Name = "Frozen expansion Sangan"; expanded.RuleCode = 123456; for (int i = 0; i < 15; i++) expanded.Setcodes[i] = (ushort)(i + 1); expanded.Setcodes[15] = 0x1234;
        var init = Init("ChainBurn", cards);
        using (var control = new UndoControl(init, Session(), 7))
        {
            var active = Active(control);
            using (var reader = new BinaryReader(new MemoryStream(active.InspectCard(26202165, 0x1234))))
            {
                string name = Encoding.UTF8.GetString(reader.ReadBytes(reader.ReadInt32()));
                Check(name == "Frozen expansion Sangan" && reader.ReadInt32() == 2468, "Child reread original database");
                reader.ReadInt32(); Check(reader.ReadInt32() == 123456 && reader.ReadBoolean(), "Child lost rule code / sixteenth setcode");
                Check(reader.ReadBoolean() && reader.ReadBoolean(), "Actual ClientCard lost normalized setcode/rule-name semantics");
            }
            Send(control, 1, Fixture("Start", "ChainBurn")); Send(control, 2, Fixture("Draw", new object[] { new int[] { 98645731, 60990740 } }));
            Check(Decision(Send(control, 3, Fixture("ActivatePot"))) == 5, "Actual persistent callback not reached");
            ulong target = control.Cursor;
            var oldOutput = Send(control, 4, new byte[] { 3 }).Single(); uint oldPid = control.ActivePid;
            var key = Key(); Check(control.Prepare(key, target), control.Failure);
            Check(control.State == BotUndoState.Ready && control.CandidatePid != 0 && control.CandidatePid != oldPid && Alive(oldPid), "Candidate not an independent real process");
            uint candidatePid = control.CandidatePid;
            Check(control.Prepare(key, target) && !control.Prepare(key, target + 1), "Duplicate Prepare changed the bound AI cursor");
            Check(!control.Accepts(oldOutput, 4) && Send(control, 5, new byte[] { 3 }).Length == 0, "Prepared participant emitted");
            var candidate = (ReplaySession)typeof(UndoControl).GetField("candidate", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(control);
            foreach (var worker in new[] { active, candidate })
            {
                var process = (Process)typeof(ReplaySession).GetField("process", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(worker);
                Check(System.Text.RegularExpressions.Regex.IsMatch(process.StartInfo.Arguments, "^--undo-(worker|candidate) --control-pipe ygopro-undo-[a-f0-9]{48}$"), "History or resources leaked in command line");
            }
            Check(control.Commit(key) && control.Commit(key) && control.CommitCount == 1, "Duplicate commit swapped twice");
            Check(control.ActivePid == candidatePid && control.RetainedPid == oldPid && Alive(oldPid) && control.Epoch == 8, "Old worker retired before Resume");
            Check(Send(control, 6, new byte[] { 3 }).Length == 0 && !control.Accepts(oldOutput, 4), "Committed participant emitted before Resume");
            Check(control.Resume(key, 8) && control.Resume(key, 8) && !Alive(oldPid), "Resume did not retire old worker exactly once");
            Check(!control.Accepts(oldOutput, 4), "Stale old process response accepted");
            var next = Send(control, 7, Fixture("SummonBackJack")); Check(Decision(next) == 7, "Candidate lost ChainBurn persistent field");
            Check(next.All(o => o.Session.SequenceEqual(Session()) && o.Epoch == 8 && o.Prompt == 7 && o.Origin == 2 && o.ProducerPid == candidatePid), "Output origin/session/epoch/prompt not bound");
        }
        Console.WriteLine("PASS W2 real active/candidate pipes: merged resources, prepare/freeze, idempotent commit, retained old PID, Resume, actual ChainBurn continuation, stale output rejection");
        using (var control = new UndoControl(Init("Dragun", cards), Session(), 7))
        {
            Send(control, 1, Fixture("Start", "Dragun")); Send(control, 2, Fixture("Draw", new object[] { new int[] { 10802915 } })); Send(control, 3, Fixture("SummonTourGuide"));
            Send(control, 4, Fixture("Effect", 10802915, (byte)0, YGOSharp.OCGWrapper.Enums.CardLocation.MonsterZone));
            var key = Key(); Check(control.Prepare(key, control.Cursor) && control.Commit(key) && control.Resume(key, 8), control.Failure);
            var next = Send(control, 5, Fixture("Selection", new object[] { new int[] { 46986414, 26202165 } }));
            Check(next.Last(o => o.Packet[0] == 1).Packet.SequenceEqual(new byte[] { 1, 1, 1 }), "Dragun pending selector lost on W2 commit");
        }
        Console.WriteLine("PASS W2 actual Dragun queued selection survives process transaction");
        using (var control = new UndoControl(init, Session(), 7))
        {
            Send(control, 1, Fixture("Start", "ChainBurn")); uint pid = control.ActivePid;
            // Fault injection changes private candidate initialization only, after
            // active initialization; real callbacks must detect the disagreement.
            var candidateInit = (BotInit)typeof(UndoControl).GetField("init", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(control);
            candidateInit.Seed++;
            var key = Key(); Check(!control.Prepare(key, control.Cursor), "Candidate tape divergence accepted");
            Check(control.State == BotUndoState.Frozen && control.ActivePid == pid && Alive(pid) && control.Epoch == 7 && control.CandidatePid == 0, "Prepare failure changed original");
            Check(Send(control, 2, new byte[] { 3 }).Length == 0, "Prepare failure unpaused participant");
            control.Abort(key); Check(control.State == BotUndoState.Running && Send(control, 3, new byte[] { 3 }).Length == 1, "Abort did not restore original");
            candidateInit.Seed--;
            key = Key(7, 2); Check(control.Prepare(key, control.Cursor) && control.Commit(key), control.Failure);
            uint installed = control.ActivePid;
            control.Pause(); control.Abort(key);
            Check(control.State == BotUndoState.Failed && control.RetainedPid == pid && Alive(pid) && Alive(installed) && !control.Resume(key, 8), "Missing acknowledgement allowed unilateral resume");
        }
        Console.WriteLine("PASS W2 divergence abort retains old PID/epoch; missing commit acknowledgement leaves both paused");
        using (var control = new UndoControl(init, Session(), 7))
        {
            uint pid = control.ActivePid; var active = Active(control);
            ((IDisposable)typeof(ReplaySession).GetField("pipe", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(active)).Dispose();
            bool failed = false; try { Send(control, 1, new byte[] { 3 }); } catch (InvalidOperationException) { failed = true; }
            Check(failed && control.State == BotUndoState.Failed && !control.Resume(Key(), 8), "Broken child pipe resumed participant");
        }
        using (var control = new UndoControl(init, Session(), 7))
        {
            using (var worker = Process.GetProcessById((int)control.ActivePid)) { worker.Kill(); worker.WaitForExit(); }
            var key = Key(); Check(!control.Prepare(key, control.Cursor), "Dead active worker prepared");
            control.Abort(key);
            Check(control.State == BotUndoState.Failed, "Abort resumed a failed active snapshot");
        }
        Console.WriteLine("PASS W2 broken child pipe and failed active snapshot stay paused");
    }
}