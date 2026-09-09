using System;
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Linq;
using System.Threading.Tasks;

namespace WindBot.Undo
{
    // Each instance owns a dedicated child. Program.Rand and executor statics never
    // coexist between active/candidate sessions in the same CLR process.
    public sealed class ReplaySession : IDisposable
    {
        private readonly BotInit init;
        private readonly object sync = new object();
        private Process process;
        private NamedPipeServerStream pipe;
        private BinaryWriter writer;
        private BinaryReader reader;
        private bool initialized, failed, disposed;
        public int ProcessId { get { lock (sync) { EnsureInitialized(); return process.Id; } } }
        public int NetworkSendCount { get; private set; }
        public int WorkerProcessId { get; private set; }
        public ReplaySession(BotInit init)
        {
            if (init == null) throw new ArgumentNullException("init");
            this.init = init.Copy();
        }
        private void Start(bool candidate)
        {
            if (disposed || failed) throw new InvalidOperationException("Replay session is closed or failed");
            if (process != null) return;
            string name = PrivatePipe.NewName();
            pipe = PrivatePipe.Server(name);
            var connected = pipe.BeginWaitForConnection(null, null);
            var info = new ProcessStartInfo(typeof(ReplaySession).Assembly.Location, (candidate ? "--undo-candidate" : "--undo-worker") + " --control-pipe " + name)
            {
                UseShellExecute = false, CreateNoWindow = true,
                RedirectStandardInput = true, RedirectStandardOutput = true, RedirectStandardError = true,
                WorkingDirectory = Path.GetDirectoryName(typeof(ReplaySession).Assembly.Location)
            };
            try
            {
                process = Process.Start(info);
                WorkerProcessId = process.Id;
                process.ErrorDataReceived += (sender, args) => { };
                process.BeginErrorReadLine();
                if (!connected.AsyncWaitHandle.WaitOne(10000))
                    throw new InvalidOperationException("Worker pipe connection timed out");
                pipe.EndWaitForConnection(connected);
                PrivatePipe.VerifyClient(pipe, process.Id);
                writer = new BinaryWriter(pipe);
                reader = new BinaryReader(pipe);
            }
            catch
            {
                failed = true;
                pipe.Dispose();
                if (process != null && !process.HasExited) { process.Kill(); process.WaitForExit(); }
                throw;
            }
            finally { connected.AsyncWaitHandle.Dispose(); }
        }
        private T Request<T>(byte command, Action<BinaryWriter> body, Func<BinaryReader, T> read)
        {
            Start(command == 2);
            try
            {
                byte[] data = ReplayRandom.Encode(w => { w.Write(command); body(w); });
                if (data.Length > WorkerWire.MaximumFrame) throw new InvalidOperationException("Worker request too large");
                // A callback hang or broken worker must not hang the host indefinitely.
                var pending = Task.Run(() =>
                {
                    WorkerWire.WriteBytes(writer, data); writer.Flush();
                    if (!reader.ReadBoolean()) throw new InvalidOperationException(reader.ReadString());
                    return WorkerWire.ReadBytes(reader);
                });
                if (!pending.Wait(TimeSpan.FromSeconds(30))) throw new InvalidOperationException("AI worker timed out");
                using (var stream = new MemoryStream(pending.Result)) using (var r = new BinaryReader(stream))
                {
                    T result = read(r);
                    NetworkSendCount = r.ReadInt32();
                    if (stream.Position != stream.Length) throw new InvalidOperationException("Trailing worker result bytes");
                    return result;
                }
            }
            catch (Exception ex)
            {
                failed = true;
                if (process != null && !process.HasExited) process.Kill();
                throw new InvalidOperationException("AI reconstruction failed: " + ex.GetBaseException().Message, ex);
            }
        }
        private void EnsureInitialized()
        {
            if (disposed || failed) throw new InvalidOperationException("Replay session is closed or failed");
            if (!initialized)
            {
                Request(1, w => w.Write(init.Encode()), r => 0);
                initialized = true;
            }
        }
        public byte[][] Dispatch(byte[] visibleMessage)
        {
            if (visibleMessage == null) throw new ArgumentNullException("visibleMessage");
            byte[] copy = (byte[])visibleMessage.Clone();
            lock (sync)
            {
                EnsureInitialized();
                return Request(3, w => WorkerWire.WriteBytes(w, copy), r =>
                {
                    int count = r.ReadInt32();
                    if (count < 0 || count > 1000000) throw new InvalidOperationException("Invalid response count");
                    var result = new byte[count][];
                    for (int i = 0; i < count; i++) result[i] = WorkerWire.ReadBytes(r);
                    return result;
                });
            }
        }
        public void Replay(TapeEntry[] entries, int stopBeforeEntry)
        {
            if (entries == null) throw new ArgumentNullException("entries");
            if (stopBeforeEntry < 1 || stopBeforeEntry > entries.Length) throw new InvalidOperationException("Invalid AI replay boundary");
            var prefix = entries.Take(stopBeforeEntry).Select(DecisionTape.Clone).ToArray();
            lock (sync)
            {
                if (initialized) throw new InvalidOperationException("Replay requires a fresh candidate");
                Request(2, w => { w.Write(init.Encode()); WorkerWire.WriteTape(w, prefix); }, r => 0);
                initialized = true;
            }
        }
        public TapeEntry[] Snapshot()
        {
            lock (sync) { EnsureInitialized(); return Request(4, w => { }, WorkerWire.ReadTape); }
        }
        public byte[] DecisionStateDigest()
        {
            lock (sync) { EnsureInitialized(); return Request(5, w => { }, WorkerWire.ReadBytes); }
        }
        public ulong Cursor
        {
            get { lock (sync) { EnsureInitialized(); return Request(7, w => { }, r => r.ReadUInt64()); } }
        }
        public byte[] InspectCard(int code, int setcode)
        {
            lock (sync) { EnsureInitialized(); return Request(6, w => { w.Write(code); w.Write(setcode); }, WorkerWire.ReadBytes); }
        }
        public void Dispose()
        {
            lock (sync)
            {
                if (disposed) return;
                disposed = true;
                if (process != null)
                {
                    if (writer != null) writer.Dispose();
                    if (!process.WaitForExit(1000)) { process.Kill(); process.WaitForExit(); }
                    if (reader != null) reader.Dispose();
                    process.Dispose();
                }
                if (pipe != null) pipe.Dispose();
            }
        }
    }
}
