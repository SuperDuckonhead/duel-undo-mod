using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using WindBot.Game.AI;

namespace WindBot.Undo
{
    // A value request. Caller supplies catalog/custom bytes from its fixed view.
    public sealed class BotSelection
    {
        public string Executor = "", DeckFile = "", Dialog = "default", Name = "WindBot-undo";
        public string Command = "", CustomDeckSource = "";
        public byte[] Catalog = new byte[0], CustomDeck;
        public FrozenBotConfig[] Configs = new FrozenBotConfig[0];
        public FrozenBotConfig AppSettings;
        public int Hand;
        public bool Chat = true, UsePreErrataEffects;
        [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
        private static extern IntPtr CommandLineToArgvW(string command, out int count);
        [DllImport("kernel32.dll")]
        private static extern IntPtr LocalFree(IntPtr memory);
        private static Dictionary<string, string> Parse(string command)
        {
            int count; var argv = CommandLineToArgvW("WindBot " + command.Replace('\'', '"'), out count);
            if (argv == IntPtr.Zero) throw new InvalidOperationException("Cannot parse fixed bot command");
            var arguments = new List<string>();
            try
            {
                for (int i = 1; i < count; ++i)
                    arguments.Add(Marshal.PtrToStringUni(Marshal.ReadIntPtr(argv, i * IntPtr.Size)));
            }
            finally { LocalFree(argv); }
            return Config.LoadArgs(arguments.ToArray());
        }
        private static void AddMissing(Dictionary<string, string> target, Dictionary<string, string> source)
        {
            foreach (var item in source) if (!target.ContainsKey(item.Key)) target.Add(item.Key, item.Value);
        }
        private static Dictionary<string, string> Merge(Dictionary<string, string> cli, FrozenBotConfig[] configs, FrozenBotConfig app)
        {
            var result = new Dictionary<string, string>(cli);
            string source;
            if (cli.TryGetValue("CONFIG", out source))
            {
                var found = configs.SingleOrDefault(c => string.Equals(c.Source, source, StringComparison.OrdinalIgnoreCase));
                if (found == null) throw new InvalidOperationException("Missing frozen Config source: " + source);
                AddMissing(result, found.Fields());
            }
            if (app != null) AddMissing(result, app.ApplicationFields());
            return result;
        }
        // Discovery never consumes RNG: a host can freeze all potentially named
        // Config inputs before the private control process makes its one choice.
        public static string[] RequiredConfigSources(string command, byte[] catalog)
        {
            var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            Action<string> add = text => { string source; if (Parse(text).TryGetValue("CONFIG", out source)) names.Add(source); };
            add(command ?? "");
            foreach (var record in CatalogRecords(catalog)) add(record[0]);
            return names.OrderBy(n => n, StringComparer.OrdinalIgnoreCase).ToArray();
        }
        private static IEnumerable<string[]> CatalogRecords(byte[] catalog)
        {
            using (var reader = new StringReader(Encoding.UTF8.GetString(catalog ?? new byte[0])))
            {
                string line;
                while ((line = reader.ReadLine()) != null)
                {
                    line = line.Trim(); if (line.Length == 0 || line[0] != '!') continue;
                    var command = reader.ReadLine(); var description = reader.ReadLine(); var flags = reader.ReadLine();
                    if (command == null || description == null || flags == null) throw new InvalidOperationException("Truncated fixed bot catalog");
                    yield return new[] { command.Trim(), flags.Trim() };
                }
            }
        }
        internal BotSelection Resolve(int seed)
        {
            if (Configs == null || Configs.Length > 64) throw new InvalidOperationException("Invalid frozen Config source count");
            var configs = Configs.Select(c => c.Copy()).ToArray();
            if (configs.Select(c => c.Source).Distinct(StringComparer.OrdinalIgnoreCase).Count() != configs.Length) throw new InvalidOperationException("Duplicate frozen Config source");
            var app = AppSettings == null ? null : AppSettings.Copy();
            var cli = Parse(Command ?? ""); var options = Merge(cli, configs, app); var rng = new Random(seed);
            string flag;
            if (options.TryGetValue("RANDOM", out flag))
            {
                var choices = CatalogRecords(Catalog).Where(r => r[1].Split(' ').Contains(flag)).Select(r => r[0]).ToArray();
                if (choices.Length == 0) throw new InvalidOperationException("No fixed Random bot matches flag");
                var selected = Parse(choices[rng.Next(choices.Length)]);
                if (selected.ContainsKey("RANDOM")) throw new InvalidOperationException("Nested Random bot selection");
                cli.Remove("RANDOM"); foreach (var item in cli) selected.Add(item.Key, item.Value);
                cli = selected; options = Merge(cli, configs, app); options.Remove("RANDOM");
            }
            var result = new BotSelection { Executor = Executor, DeckFile = DeckFile, Dialog = Dialog, Name = Name, Hand = Hand, Chat = Chat, UsePreErrataEffects = UsePreErrataEffects,
                Configs = configs, AppSettings = app, CustomDeckSource = CustomDeckSource, CustomDeck = CustomDeck == null ? null : (byte[])CustomDeck.Clone() };
            foreach (var option in options)
            {
                switch (option.Key.ToUpperInvariant())
                {
                    case "CONFIG": break; // already resolved from the frozen binding
                    case "NAME": result.Name = option.Value; break;
                    case "DECK": result.Executor = option.Value; break;
                    case "DECKFILE":
                        // A selected file is represented by supplied bytes, never a child disk path.
                        if (result.CustomDeck == null) result.DeckFile = option.Value;
                        break;
                    case "DIALOG": result.Dialog = option.Value; break;
                    case "HAND": result.Hand = option.Value.StartsWith("0x") ? Convert.ToInt32(option.Value, 16) : Convert.ToInt32(option.Value); break;
                    case "CHAT": result.Chat = bool.Parse(option.Value); break;
                    case "USEPREERRATAEFFECTS": result.UsePreErrataEffects = bool.Parse(option.Value); break;
                    case "DEBUG": if (bool.Parse(option.Value)) throw new InvalidOperationException("Debug side effects unsupported in isolated bot"); break;
                    case "HOST": case "PORT": case "RUNTIMEROOT": case "HOSTINFO": case "VERSION": case "DBPATH": case "SERVERMODE": case "SERVERPORT":
                        if (cli.ContainsKey(option.Key)) throw new InvalidOperationException("Network/runtime options must be host-owned for isolated bot");
                        break; // Inherited transport fields cannot override host authority.
                    default:
                        if (cli.ContainsKey(option.Key)) throw new InvalidOperationException("Unsupported fixed bot option: " + option.Key);
                        break; // Config can contain fields unrelated to this participant.
                }
            }
            var metadata = DecksManager.ResolveSelection(result.Executor, rng);
            result.Executor = metadata[0]; if (string.IsNullOrEmpty(result.DeckFile)) result.DeckFile = metadata[1];
            if (result.Hand < 0 || result.Hand > 3 || string.IsNullOrEmpty(result.Name) || result.Name.Length > 19 || result.Name.IndexOf('\0') >= 0)
                throw new InvalidOperationException("Invalid fixed bot name/hand");
            if (result.CustomDeck != null && (string.IsNullOrEmpty(result.CustomDeckSource) || result.CustomDeck.Length > 1024 * 1024))
                throw new InvalidOperationException("Missing custom deck binding or excessive deck size");
            return result;
        }
    }
}