using System;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using WindBot.Game;

namespace WindBot.Undo
{
    public sealed class BotInit
    {
        public string Executor;
        public int Seed;
        public byte[] Deck;
        public byte[] ResourceDigest;
        public byte[] Options;

        public static BotInit Capture(string runtimeRoot, string databasePath, string executor, string deckFile, string dialog, int seed, bool chat, bool usePreErrataEffects = false)
        {
            var settings = new ReplayOptions { RuntimeRoot = Path.GetFullPath(runtimeRoot), DatabasePath = Path.GetFullPath(databasePath), DeckFile = deckFile, Dialog = dialog, Chat = chat, UsePreErrataEffects = usePreErrataEffects };
            var init = new BotInit { Executor = executor, Seed = seed, Options = settings.Encode(), Deck = File.ReadAllBytes(settings.DeckPath) };
            init.ResourceDigest = settings.ResourceHash();
            return init;
        }
        internal byte[] Encode()
        {
            return ReplayRandom.Encode(w => { w.Write(Executor); w.Write(Seed); WorkerWire.WriteBytes(w, Deck); WorkerWire.WriteBytes(w, ResourceDigest); WorkerWire.WriteBytes(w, Options); });
        }
        internal static BotInit Read(BinaryReader r)
        {
            return new BotInit { Executor = r.ReadString(), Seed = r.ReadInt32(), Deck = WorkerWire.ReadBytes(r), ResourceDigest = WorkerWire.ReadBytes(r), Options = WorkerWire.ReadBytes(r) };
        }
        internal BotInit Copy()
        {
            if (string.IsNullOrEmpty(Executor) || Deck == null || ResourceDigest == null || ResourceDigest.Length != 32 || Options == null)
                throw new InvalidOperationException("Incomplete fixed BotInit");
            using (var r = new BinaryReader(new MemoryStream(Encode()))) return Read(r);
        }
    }

    internal sealed class ReplayOptions
    {
        internal string RuntimeRoot, DatabasePath, DeckFile, Dialog;
        internal bool Chat, UsePreErrataEffects;
        internal string DeckPath { get { return Path.Combine(RuntimeRoot, "Decks", DeckFile + ".ydk"); } }
        internal byte[] Encode()
        {
            return ReplayRandom.Encode(w => { w.Write(1); w.Write(RuntimeRoot); w.Write(DatabasePath); w.Write(DeckFile); w.Write(Dialog); w.Write(Chat); w.Write(UsePreErrataEffects); });
        }
        internal static ReplayOptions Decode(byte[] bytes)
        {
            using (var stream = new MemoryStream(bytes)) using (var r = new BinaryReader(stream))
            {
                if (r.ReadInt32() != 1) throw new InvalidOperationException("Unsupported BotInit options");
                var settings = new ReplayOptions { RuntimeRoot = r.ReadString(), DatabasePath = r.ReadString(), DeckFile = r.ReadString(), Dialog = r.ReadString(), Chat = r.ReadBoolean(), UsePreErrataEffects = r.ReadBoolean() };
                if (stream.Position != stream.Length || !Path.IsPathRooted(settings.RuntimeRoot) || !Path.IsPathRooted(settings.DatabasePath) ||
                    Path.GetFileName(settings.DeckFile) != settings.DeckFile || Path.GetFileName(settings.Dialog) != settings.Dialog)
                    throw new InvalidOperationException("Invalid fixed resource paths");
                return settings;
            }
        }
        internal string[] ResourcePaths()
        {
            string assembly = typeof(BotInit).Assembly.Location;
            string binaryRoot = Path.GetDirectoryName(assembly);
#if UNDO_BUILD
            binaryRoot = Path.Combine(binaryRoot, "undo-deps");
#endif
            return new[] { DatabasePath, Path.Combine(RuntimeRoot, "bots.json"), DeckPath, Path.Combine(RuntimeRoot, "Dialogs", Dialog + ".json"), assembly, assembly + ".config", Path.Combine(binaryRoot, "x86", "sqlite3.dll"), Path.Combine(binaryRoot, "x64", "sqlite3.dll") };
        }
        internal byte[] ResourceHash()
        {
            using (var sha = SHA256.Create())
            {
                byte[] manifest = ReplayRandom.Encode(w =>
                {
                    foreach (string path in ResourcePaths())
                    {
                        // Logical order, not absolute installation paths, defines this view.
                        using (var input = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read))
                            WorkerWire.WriteBytes(w, sha.ComputeHash(input));
                    }
                });
                return sha.ComputeHash(manifest);
            }
        }
        internal void Validate(BotInit init)
        {
            if (!init.ResourceDigest.SequenceEqual(ResourceHash()) || !init.Deck.SequenceEqual(File.ReadAllBytes(DeckPath)))
                throw new InvalidOperationException("Bot initialization resources changed");
        }
        internal WindBotInfo Info(BotInit init)
        {
            return new WindBotInfo { Name = "WindBot-undo", Deck = init.Executor, DeckFile = DeckFile, Dialog = Dialog, Hand = 0, Chat = Chat, Debug = false };
        }
    }
}
