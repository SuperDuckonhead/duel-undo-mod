using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Diagnostics;
using WindBot;
using WindBot.Undo;
static class RuntimeTests
{
    static void Check(bool value, string message) { if(!value) throw new InvalidOperationException(message); }
    public static void Run()
    {
        string root = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "runtime-probe-" + Process.GetCurrentProcess().Id);
        Directory.CreateDirectory(root);
        File.WriteAllText(Path.Combine(root, "bot.conf"), "!selected\nDeck=ChainBurn Name='two words'\ncontrolled\nMATCH\n!other\nDeck=Dragun\nother\nOTHER\n");
        var expand = typeof(Program).GetMethod("ExpandRandomBot", BindingFlags.NonPublic | BindingFlags.Static);
        Check(expand != null, "Adapted direct launch must preserve legacy Random flag selection");
        string[] args = (string[])expand.Invoke(null, new object[] { new[] { "Random=MATCH", "Port=7912", "Hand=1" }, root });
        Check(args.SequenceEqual(new[] { "Deck=ChainBurn", "Name=two words", "Port=7912", "Hand=1" }), "Random flag and quoted bot command translation changed");
        var cwd = Directory.GetCurrentDirectory();
        var originalError = Console.Error;
        var captured = new StringWriter();
        try {
            Directory.SetCurrentDirectory(root);
            Console.SetError(captured);
            try { Program.ReadFile("Decks", "missing-controlled", "ydk"); }
            catch(IOException) { }
            Check(captured.ToString().Contains(Path.GetFullPath("../deck/missing-controlled.ydk")), "Missing live AI data must report actual lookup path");
        } finally { Directory.SetCurrentDirectory(cwd); Console.SetError(originalError); }
        var type = typeof(BotInit).Assembly.GetType("Mono.Data.Sqlite.SqliteConnection", true);
        using(var connection = (IDisposable)Activator.CreateInstance(type, new object[] { "Data Source=:memory:" })) {
            type.GetMethod("Open").Invoke(connection, null);
            var sqlite = Process.GetCurrentProcess().Modules.Cast<ProcessModule>().Single(m => string.Equals(m.ModuleName, "sqlite3.dll", StringComparison.OrdinalIgnoreCase));
            string expected = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "undo-deps", Environment.Is64BitProcess ? "x64" : "x86", "sqlite3.dll");
            Check(string.Equals(sqlite.FileName, expected, StringComparison.OrdinalIgnoreCase), "Native SQLite loaded outside private undo-deps architecture directory");
        }
        Console.WriteLine("PASS direct bot Random command translation and actual loaded private native SQLite module path");
    }
}
