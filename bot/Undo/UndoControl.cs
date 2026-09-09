using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;

namespace WindBot.Undo
{
    public enum BotUndoState : byte { Running, Frozen, Ready, Committed, Failed }
    public sealed class BotTxKey
    {
        public byte[] Session = new byte[16], TargetDigest = new byte[32];
        public ulong Epoch, Request, TargetIndex;
        internal void Write(BinaryWriter w) { w.Write(Session); w.Write(Epoch); w.Write(Request); w.Write(TargetIndex); w.Write(TargetDigest); }
        internal static BotTxKey Read(BinaryReader r)
        {
            return new BotTxKey { Session = r.ReadBytes(16), Epoch = r.ReadUInt64(), Request = r.ReadUInt64(), TargetIndex = r.ReadUInt64(), TargetDigest = r.ReadBytes(32) };
        }
        internal BotTxKey Copy()
        {
            if (Session == null || Session.Length != 16 || TargetDigest == null || TargetDigest.Length != 32) throw new InvalidOperationException("Invalid N1 transaction key");
            using (var r = new BinaryReader(new MemoryStream(ReplayRandom.Encode(Write)))) return Read(r);
        }
        internal bool Same(BotTxKey other)
        {
            return other != null && Epoch == other.Epoch && Request == other.Request && TargetIndex == other.TargetIndex && Session.SequenceEqual(other.Session) && TargetDigest.SequenceEqual(other.TargetDigest);
        }
    }
    public sealed class BotOutput
    {
        public byte[] Session, Packet;
        public ulong Epoch, Prompt;
        public byte Origin = 2; // undo::Origin::Bot
        public uint ProducerPid;
        internal void Write(BinaryWriter w)
        {
            w.Write(Session); w.Write(Epoch); w.Write(Prompt); w.Write(Origin); w.Write(ProducerPid); WorkerWire.WriteBytes(w, Packet);
        }
    }
    public sealed class UndoControl : IDisposable
    {
        private readonly object sync = new object();
        private readonly BotInit init;
        private readonly byte[] session;
        private ReplaySession active, candidate, retained;
        private BotTxKey transaction, completed;
        private ulong highestRequest, candidateCursor;
        private bool disposed;
        public BotUndoState State { get; private set; }
        public ulong Epoch { get; private set; }
        public ulong Cursor { get; private set; }
        public uint CommitCount { get; private set; }
        public string Failure { get; private set; }
        public uint ActivePid { get { return active == null ? 0 : (uint)active.WorkerProcessId; } }
        public uint CandidatePid { get { return candidate == null ? 0 : (uint)candidate.WorkerProcessId; } }
        public uint RetainedPid { get { return retained == null ? 0 : (uint)retained.WorkerProcessId; } }
        public UndoControl(BotInit init, byte[] session, ulong epoch)
        {
            if (session == null || session.Length != 16 || init.MergedCards.Length == 0) throw new InvalidOperationException("W2 requires bound merged resources and session");
            this.init = init.Copy(); this.session = (byte[])session.Clone(); Epoch = epoch;
            active = new ReplaySession(this.init);
            try { Cursor = active.Cursor; State = BotUndoState.Running; }
            catch { active.Dispose(); active = null; throw; }
        }
        public static bool CanEmitResponse(BotUndoState state, ulong responseEpoch, ulong activeEpoch)
        { return state == BotUndoState.Running && responseEpoch == activeEpoch; }
        public BotOutput[] Dispatch(byte[] inputSession, ulong epoch, ulong prompt, byte[] packet)
        {
            lock (sync)
            {
                if (disposed || !CanEmitResponse(State, epoch, Epoch) || !session.SequenceEqual(inputSession)) return new BotOutput[0];
                try
                {
                    var packets = active.Dispatch(packet); Cursor = active.Cursor;
                    return packets.Select(p => new BotOutput { Session = (byte[])session.Clone(), Epoch = Epoch, Prompt = prompt, ProducerPid = ActivePid, Packet = (byte[])p.Clone() }).ToArray();
                }
                catch (Exception ex) { Failure = ex.Message; State = BotUndoState.Failed; throw; }
            }
        }
        // Also use at the last delivery boundary; a previously returned output can
        // become stale while a host transaction is outstanding.
        public bool Accepts(BotOutput output, ulong currentPrompt)
        {
            lock (sync) return !disposed && output != null && output.Origin == 2 && CanEmitResponse(State, output.Epoch, Epoch) && session.SequenceEqual(output.Session) && output.Prompt == currentPrompt && output.ProducerPid == ActivePid;
        }
        public bool Prepare(BotTxKey key, ulong aiCursor)
        {
            lock (sync)
            {
                if (disposed) return false;
                if (transaction != null && transaction.Same(key)) return State == BotUndoState.Ready && aiCursor == candidateCursor;
                if (State != BotUndoState.Running || key == null || !session.SequenceEqual(key.Session) || key.Epoch != Epoch || Epoch == ulong.MaxValue || key.Request == 0 || key.Request <= highestRequest || key.TargetDigest == null || key.TargetDigest.Length != 32 || key.TargetDigest.All(b => b == 0)) return false;
                transaction = key.Copy(); highestRequest = key.Request;
                State = BotUndoState.Frozen; Failure = "";
                bool activeSnapshotComplete = false;
                try
                {
                    // All queued Dispatch calls are serialized by sync. New ones
                    // observe Frozen; this snapshot ends after a complete callback.
                    var tape = active.Snapshot(); Cursor = (ulong)tape.Length; activeSnapshotComplete = true;
                    if (aiCursor > Cursor || aiCursor > int.MaxValue) throw new InvalidOperationException("Invalid AI checkpoint cursor");
                    candidate = new ReplaySession(init);
                    candidate.Replay(tape, (int)aiCursor); candidateCursor = aiCursor;
                    State = BotUndoState.Ready;
                    return true;
                }
                catch (Exception ex)
                {
                    if (candidate != null) candidate.Dispose(); candidate = null;
                    Failure = ex.Message; // Candidate failure permits Abort; active failure does not.
                    State = activeSnapshotComplete ? BotUndoState.Frozen : BotUndoState.Failed; return false;
                }
            }
        }
        public bool Commit(BotTxKey key)
        {
            lock (sync)
            {
                if (disposed || State == BotUndoState.Failed) return false;
                if (transaction == null) return State == BotUndoState.Running && completed != null && completed.Same(key);
                if (!transaction.Same(key)) return false;
                if (State == BotUndoState.Committed) return true;
                if (State != BotUndoState.Ready) return false;
                retained = active; active = candidate; candidate = null;
                Epoch = key.Epoch + 1; Cursor = candidateCursor; CommitCount++; State = BotUndoState.Committed;
                // No callbacks/output run as part of install. Old process remains.
                return true;
            }
        }
        public bool Resume(BotTxKey key, ulong installedEpoch)
        {
            lock (sync)
            {
                if (disposed || State == BotUndoState.Failed) return false;
                if (transaction == null) return State == BotUndoState.Running && completed != null && completed.Same(key) && Epoch == installedEpoch;
                if (State != BotUndoState.Committed || !transaction.Same(key) || installedEpoch != Epoch) return false;
                // Only the trusted host calls this after ALL CommitAcks.
                retained.Dispose(); retained = null;

                completed = transaction; transaction = null; State = BotUndoState.Running;
                return true;
            }
        }
        public void Abort(BotTxKey key)
        {
            lock (sync)
            {
                if (disposed || transaction == null || !transaction.Same(key)) return;
                if (State == BotUndoState.Committed || State == BotUndoState.Failed) { State = BotUndoState.Failed; return; }
                if (candidate != null) candidate.Dispose(); candidate = null;
                transaction = null; State = BotUndoState.Running;
            }
        }
        public void Pause() { lock (sync) { if (!disposed) State = BotUndoState.Failed; } }
        public void Dispose()
        {
            lock (sync)
            {
                if (disposed) return; disposed = true; State = BotUndoState.Failed;
                if (candidate != null) candidate.Dispose(); if (retained != null) retained.Dispose(); if (active != null) active.Dispose();
                candidate = retained = active = null;
            }
        }
        private void WriteStatus(BinaryWriter w)
        {
            w.Write((byte)State); w.Write(Epoch); w.Write(ActivePid); w.Write(CandidatePid); w.Write(RetainedPid); w.Write(Cursor); w.Write(CommitCount); FrozenCardView.WriteText(w, Failure ?? "");
        }
        internal static int Run(string pipeName)
        {
            using (var pipe = PrivatePipe.ConnectToCreator(pipeName))
            using (var reader = new BinaryReader(pipe)) using (var writer = new BinaryWriter(pipe))
            {
                Console.SetOut(TextWriter.Null); Console.SetError(TextWriter.Null);
                UndoControl control = null;
                try
                {
                    while (true)
                    {
                        byte[] request = WorkerWire.ReadBytes(reader);
                        byte[] response = ReplayRandom.Encode(w =>
                        {
                            using (var stream = new MemoryStream(request)) using (var r = new BinaryReader(stream))
                            {
                                byte command = r.ReadByte(); bool accepted = true; BotOutput[] outputs = new BotOutput[0];
                                if (command == 1)
                                {
                                    if (control != null) throw new InvalidOperationException("Control already initialized");
                                    byte[] sid = r.ReadBytes(16); ulong epoch = r.ReadUInt64();
                                    byte[] engine = r.ReadBytes(32), resources = r.ReadBytes(32), cards = WorkerWire.ReadBytes(r);
                                    string root = FrozenCardView.ReadText(r), executor = FrozenCardView.ReadText(r), deck = FrozenCardView.ReadText(r), dialog = FrozenCardView.ReadText(r);
                                    int seed = r.ReadInt32(); bool chat = r.ReadBoolean(), pre = r.ReadBoolean();
                                    control = new UndoControl(BotInit.CaptureMerged(root, executor, deck, dialog, seed, chat, pre, cards, engine, resources), sid, epoch);
                                }
                                else
                                {
                                    if (control == null) throw new InvalidOperationException("Control not initialized");
                                    if (command == 2) outputs = control.Dispatch(r.ReadBytes(16), r.ReadUInt64(), r.ReadUInt64(), WorkerWire.ReadBytes(r));
                                    else if (command == 3) accepted = control.Prepare(BotTxKey.Read(r), r.ReadUInt64());
                                    else if (command == 4) accepted = control.Commit(BotTxKey.Read(r));
                                    else if (command == 5) control.Abort(BotTxKey.Read(r));
                                    else if (command == 6) accepted = control.Resume(BotTxKey.Read(r), r.ReadUInt64());
                                    else if (command == 8) control.Pause();
                                    else if (command != 7) throw new InvalidOperationException("Unknown private control command");
                                }
                                if (stream.Position != stream.Length) throw new InvalidOperationException("Trailing control bytes");
                                w.Write(accepted); control.WriteStatus(w); w.Write(outputs.Length); foreach (var output in outputs) output.Write(w);
                            }
                        });
                        writer.Write(true); WorkerWire.WriteBytes(writer, response); writer.Flush();
                    }
                }
                catch (Exception ex)
                {
                    if (control != null) control.Pause();
                    try { writer.Write(false); writer.Write(ex.Message); writer.Flush(); } catch (IOException) { }
                    // A lost control pipe cannot authorize either process to resume.
                    // Retain both frozen until the creating host exits or closes its
                    // kill-on-close job. This also preserves uncertain Commit state.
                    try { using (var parent = Process.GetProcessById(PrivatePipe.ParentPid())) parent.WaitForExit(); } catch (ArgumentException) { }
                    return 1;
                }
                finally { if (control != null) control.Dispose(); }
            }
        }
    }
}