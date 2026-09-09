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
            var options = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            try
            {
                for (int i = 1; i < count; ++i)
                {
                    string item = Marshal.PtrToStringUni(Marshal.ReadIntPtr(argv, i * IntPtr.Size));
                    int split = item.IndexOf('=');
                    if (split < 1) throw new InvalidOperationException("Expected bot key=value option");
                    options.Add(item.Substring(0, split), item.Substring(split + 1));
                }
            }
            finally { LocalFree(argv); }
            return options;
        }
        internal BotSelection Resolve(int seed)
        {
            var options = Parse(Command ?? ""); var rng = new Random(seed);
            string flag;
            if (options.TryGetValue("Random", out flag))
            {
                var choices = new List<string>();
                using (var reader = new StringReader(Encoding.UTF8.GetString(Catalog ?? new byte[0])))
                {
                    string line;
                    while ((line = reader.ReadLine()) != null)
                    {
                        line = line.Trim(); if (line.Length == 0 || line[0] != '!') continue;
                        var command = reader.ReadLine(); var description = reader.ReadLine(); var flags = reader.ReadLine();
                        if (command == null || description == null || flags == null) throw new InvalidOperationException("Truncated fixed bot catalog");
                        if (flags.Trim().Split(' ').Contains(flag)) choices.Add(command.Trim());
                    }
                }
                if (choices.Count == 0) throw new InvalidOperationException("No fixed Random bot matches flag");
                var selected = Parse(choices[rng.Next(choices.Count)]);
                options.Remove("Random"); foreach (var item in options) selected.Add(item.Key, item.Value);
                if (selected.ContainsKey("Random")) throw new InvalidOperationException("Nested Random bot selection");
                options = selected;
            }
            var result = new BotSelection { Executor = Executor, DeckFile = DeckFile, Dialog = Dialog, Name = Name, Hand = Hand, Chat = Chat, UsePreErrataEffects = UsePreErrataEffects,
                CustomDeckSource = CustomDeckSource, CustomDeck = CustomDeck == null ? null : (byte[])CustomDeck.Clone() };
            foreach (var option in options)
            {
                switch (option.Key.ToUpperInvariant())
                {
                    case "NAME": result.Name = option.Value; break;
                    case "DECK": result.Executor = option.Value; break;
                    case "DECKFILE":
                        // A selected file is represented by supplied bytes, never a child disk path.
                        if (result.CustomDeck == null) result.DeckFile = option.Value;
                        break;
                    case "DIALOG": result.Dialog = option.Value; break;
                    case "HAND": result.Hand = int.Parse(option.Value, System.Globalization.CultureInfo.InvariantCulture); break;
                    case "CHAT": result.Chat = bool.Parse(option.Value); break;
                    case "USEPREERRATAEFFECTS": result.UsePreErrataEffects = bool.Parse(option.Value); break;
                    case "DEBUG": if (bool.Parse(option.Value)) throw new InvalidOperationException("Debug side effects unsupported in isolated bot"); break;
                    case "HOST": case "PORT": case "RUNTIMEROOT": case "HOSTINFO": case "VERSION":
                        throw new InvalidOperationException("Network/runtime options must be host-owned for isolated bot");
                    default: throw new InvalidOperationException("Unsupported fixed bot option: " + option.Key);
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