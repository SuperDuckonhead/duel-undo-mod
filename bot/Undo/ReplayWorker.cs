using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using WindBot.Game;
using WindBot.Game.AI;
using YGOSharp.OCGWrapper;

namespace WindBot.Undo
{
    internal sealed class ReplayWorker
    {
        private readonly DecisionTape tape;
        private readonly GameClient game;
        private readonly List<FileStream> resourceLeases = new List<FileStream>();
        private readonly List<byte[]> output = new List<byte[]>();
        private byte[] currentMessage = new byte[0];
        internal ReplayWorker(BotInit init, DecisionTape tape)
        {
            this.tape = tape;
            ReplayOptions options = ReplayOptions.Decode(init.Options);
            foreach (string path in options.ResourcePaths())
                resourceLeases.Add(new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read));
            options.Validate(init);
            tape.Observe(new TapeEntry { Kind = TapeKind.External, Call = "BotInit/v1", Input = init.Encode(), Output = new byte[0] });
            Program.ResourceRoot = options.RuntimeRoot;
            Program.Rand = new ReplayRandom(init.Seed, tape);
            Program.ServerMode = false;
            Config.Load(new[] { "UsePreErrataEffects=" + options.UsePreErrataEffects.ToString() });
            DecksManager.Init(Program.Rand);
            if (!DecksManager.HasDeck(init.Executor)) throw new InvalidOperationException("Unknown fixed executor");
            NamedCardsManager.Init(options.DatabasePath, true);
            game = new GameClient(options.Info(init));
            game.StartOffline(OnOutput);
            options.Validate(init);
        }
        private void OnOutput(byte[] bytes)
        {
            tape.Observe(new TapeEntry { Kind = TapeKind.Decision, Call = "CTOS", Input = currentMessage, Output = bytes });
            output.Add((byte[])bytes.Clone());
        }
        internal byte[][] Dispatch(byte[] message)
        {
            if (message.Length == 0 || message.Length > 65535) throw new InvalidOperationException("Invalid visible packet length");
            output.Clear();
            currentMessage = (byte[])message.Clone();
            tape.Observe(new TapeEntry { Kind = TapeKind.Message, Call = "STOC", Input = currentMessage, Output = new byte[0] });
            game.DispatchOffline(currentMessage);
            return output.Select(p => (byte[])p.Clone()).ToArray();
        }
        internal byte[] Digest()
        {
            return StateFingerprint.Compute(game.OfflineState(), tape.Snapshot());
        }
        internal static int Run()
        {
            // Keep protocol bytes separate from legacy logger/Executor console output.
            using (var input = new BinaryReader(Console.OpenStandardInput()))
            using (var output = new BinaryWriter(Console.OpenStandardOutput()))
            {
                System.Threading.Thread.CurrentThread.CurrentCulture = System.Globalization.CultureInfo.InvariantCulture;
                System.Threading.Thread.CurrentThread.CurrentUICulture = System.Globalization.CultureInfo.InvariantCulture;
                Console.SetOut(TextWriter.Null);
                Console.SetError(TextWriter.Null);
                ReplayWorker worker = null;
                DecisionTape tape = null;
                while (true)
                {
                    byte[] request;
                    try { request = WorkerWire.ReadBytes(input); }
                    catch (EndOfStreamException) { return 0; }
                    try
                    {
                        byte[] result = ReplayRandom.Encode(w =>
                        {
                            using (var stream = new MemoryStream(request)) using (var r = new BinaryReader(stream))
                            {
                                byte command = r.ReadByte();
                                if (command == 1 || command == 2)
                                {
                                    if (worker != null) throw new InvalidOperationException("Worker already initialized");
                                    var init = BotInit.Read(r);
                                    var expected = command == 2 ? WorkerWire.ReadTape(r) : null;
                                    tape = new DecisionTape();
                                    if (expected != null) tape.BeginReplay(expected);
                                    worker = new ReplayWorker(init, tape);
                                    if (expected != null)
                                    {
                                        while (tape.Cursor < expected.Length)
                                        {
                                            var next = expected[tape.Cursor];
                                            if (next.Kind != TapeKind.Message || next.Call != "STOC")
                                                throw new InvalidOperationException("Replay boundary is not a message driver");
                                            worker.Dispatch(next.Input);
                                        }
                                        tape.RequireEnd();
                                        tape.ContinueRecording();
                                    }
                                }
                                else
                                {
                                    if (worker == null) throw new InvalidOperationException("Worker not initialized");
                                    if (command == 3)
                                    {
                                        byte[][] responses = worker.Dispatch(WorkerWire.ReadBytes(r));
                                        w.Write(responses.Length);
                                        foreach (var response in responses) WorkerWire.WriteBytes(w, response);
                                    }
                                    else if (command == 4) WorkerWire.WriteTape(w, tape.Snapshot());
                                    else if (command == 5) WorkerWire.WriteBytes(w, worker.Digest());
                                    else throw new InvalidOperationException("Unknown W1 worker command");
                                }
                                if (stream.Position != stream.Length) throw new InvalidOperationException("Trailing worker command bytes");
                                w.Write(worker.game.Connection.NetworkSendCount);
                            }
                        });
                        output.Write(true); WorkerWire.WriteBytes(output, result); output.Flush();
                    }
                    catch (Exception ex)
                    {
                        output.Write(false); output.Write(ex.GetBaseException().GetType().Name + ": " + ex.GetBaseException().Message); output.Flush();
                        return 1; // A partially executed callback is never reusable.
                    }
                }
            }
        }
    }
}