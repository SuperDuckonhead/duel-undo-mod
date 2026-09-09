using System;
using System.IO;
using System.Threading;
using System.Net;
using System.Web;
using System.Diagnostics;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using WindBot.Game;
using WindBot.Game.AI;
using YGOSharp.OCGWrapper;

namespace WindBot
{
    public class Program
    {
        internal static Random Rand;
        internal static bool ServerMode;
        internal static string ResourceRoot;

        internal static void Main(string[] args)
        {
#if UNDO_BUILD
            try { MainCore(args); }
            catch(Exception error) { Console.Error.WriteLine(error.Message); Environment.ExitCode = 1; }
#else
            MainCore(args);
#endif
        }

        private static void MainCore(string[] args)
        {
            if (args.Length == 3 && args[1] == "--control-pipe" && (args[0] == "--undo-worker" || args[0] == "--undo-candidate"))
            {
                Environment.ExitCode = Undo.ReplayWorker.Run(args[2]);
                return;
            }
            if (args.Length == 3 && args[0] == "--undo-control" && args[1] == "--control-pipe")
            {
                Environment.ExitCode = Undo.UndoControl.Run(args[2]);
                return;
            }
            if (args.Length == 1 && args[0] == "--undo-replay-worker")
            {
                Environment.ExitCode = Undo.ReplayWorker.Run();
                return;
            }
#if UNDO_BUILD
            Console.OutputEncoding = new System.Text.UTF8Encoding(false);
            Console.InputEncoding = new System.Text.UTF8Encoding(false);
            Directory.SetCurrentDirectory(AppDomain.CurrentDomain.BaseDirectory);
#endif
            Logger.WriteLine("WindBot starting...");

            Config.Load(args);

#if UNDO_BUILD
            string runtimeRoot = Path.GetFullPath(Config.GetString("RuntimeRoot", Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "..")));
            Directory.SetCurrentDirectory(Path.Combine(runtimeRoot, "WindBot"));
            args = ExpandRandomBot(args, runtimeRoot);
            Config.Load(args);
            string databasePath = Config.GetString("DbPath", Path.Combine(runtimeRoot, "cards.cdb"));
#else
            string databasePath = Config.GetString("DbPath", "cards.cdb");
#endif

            InitDatas(databasePath);

            ServerMode = Config.GetBool("ServerMode", false);

            if (ServerMode)
            {
                // Run in server mode, provide a http interface to create bot.
                int serverPort = Config.GetInt("ServerPort", 2399);
                RunAsServer(serverPort);
            }
            else
            {
                // Join the host specified on the command line.
                if (args.Length == 0)
                {
                    Logger.WriteErrorLine("=== WARN ===", null);
                    Logger.WriteLine("No input found, trying to connect to localhost YGOPro host.");
                    Logger.WriteLine("If it fails, the program will quit silently.");
                }
                RunFromArgs();
            }
        }

#if UNDO_BUILD
        [DllImport("shell32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern IntPtr CommandLineToArgvW(string command, out int count);
        [DllImport("kernel32.dll")]
        private static extern IntPtr LocalFree(IntPtr memory);

        private static string[] ExpandRandomBot(string[] args, string runtimeRoot)
        {
            string random = args.FirstOrDefault(a => a.StartsWith("Random=", StringComparison.OrdinalIgnoreCase));
            if(random == null) return args;
            string flag = random.Substring(7);
            var commands = new List<string>();
            // Preserve the old wrapper's four-line records and exact flag matching.
            using(var reader = new StreamReader(Path.Combine(runtimeRoot, "bot.conf"))) {
                while(!reader.EndOfStream) {
                    string line = reader.ReadLine().Trim();
                    if(line.Length == 0 || line[0] != '!') continue;
                    string command = reader.ReadLine();
                    string description = reader.ReadLine();
                    string flags = reader.ReadLine();
                    if(command == null || description == null || flags == null) break;
                    if(flags.Trim().Split(' ').Contains(flag)) commands.Add(command.Trim());
                }
            }
            string chosen = commands.Count == 0 ? "" : commands[new Random().Next(commands.Count)];
            int count;
            IntPtr argv = CommandLineToArgvW("WindBot " + chosen.Replace('\'', '"'), out count);
            if(argv == IntPtr.Zero) throw new InvalidOperationException("Cannot parse selected bot command");
            var result = new List<string>();
            try {
                for(int i = 1; i < count; ++i) result.Add(Marshal.PtrToStringUni(Marshal.ReadIntPtr(argv, i * IntPtr.Size)));
            } finally { LocalFree(argv); }
            result.AddRange(args.Where(a => !a.StartsWith("Random=", StringComparison.OrdinalIgnoreCase)));
            return result.ToArray();
        }
#endif

        public static void InitDatas(string databasePath)
        {
            Rand = new Random();
            DecksManager.Init();
            string absolutePath = Path.GetFullPath(databasePath);
#if !UNDO_BUILD
            if (!File.Exists(absolutePath))
                // In case windbot is placed in a folder under ygopro folder
                absolutePath = Path.GetFullPath("../" + databasePath);
            if (!File.Exists(absolutePath))
                // In case windbot is placed in a folder under ygopro2 folder
                absolutePath = Path.GetFullPath("../cdb/" + databasePath);
#endif
            if (!File.Exists(absolutePath))
            {
#if UNDO_BUILD
                throw new FileNotFoundException("Cannot find cards database: " + absolutePath, absolutePath);
#else
                Logger.WriteErrorLine("Can't find cards database file.", null);
                Logger.WriteErrorLine("Please place cards.cdb next to WindBot.exe or Bot.exe .", null);
                Logger.WriteLine("Press any key to quit...");
                Console.ReadKey();
                System.Environment.Exit(1);
#endif
            }
            NamedCardsManager.Init(absolutePath);
        }

        private static void RunFromArgs()
        {
            WindBotInfo Info = new WindBotInfo();
            Info.Name = Config.GetString("Name", Info.Name);
            Info.Deck = Config.GetString("Deck", Info.Deck);
            Info.DeckFile = Config.GetString("DeckFile", Info.DeckFile);
            Info.Dialog = Config.GetString("Dialog", Info.Dialog);
            Info.Host = Config.GetString("Host", Info.Host);
            Info.Port = Config.GetInt("Port", Info.Port);
            Info.HostInfo = Config.GetString("HostInfo", Info.HostInfo);
            Info.Version = Config.GetInt("Version", Info.Version);
            Info.Hand = Config.GetInt("Hand", Info.Hand);
            Info.Debug = Config.GetBool("Debug", Info.Debug);
            Info.Chat = Config.GetBool("Chat", Info.Chat);
            Run(Info);
        }

        private static void RunAsServer(int ServerPort)
        {
            using (HttpListener MainServer = new HttpListener())
            {
                MainServer.AuthenticationSchemes = AuthenticationSchemes.Anonymous;
                MainServer.Prefixes.Add("http://+:" + ServerPort + "/");
                MainServer.Start();
                Logger.WriteLine("WindBot server start successed.");
                Logger.WriteLine("HTTP GET http://127.0.0.1:" + ServerPort + "/?name=WindBot&host=127.0.0.1&port=7911 to call the bot.");
                while (true)
                {
                    try
                    {
                    HttpListenerContext ctx = MainServer.GetContext();
                    var queryParams = HttpUtility.ParseQueryString(ctx.Request.Url.Query);

                    WindBotInfo Info = new WindBotInfo();
                    Info.Name = queryParams.Get("name");
                    Info.Deck = queryParams.Get("deck");
                    Info.Host = queryParams.Get("host");
                    string port = queryParams.Get("port");
                    if (port != null)
                        Info.Port = Int32.Parse(port);
                    string deckfile = queryParams.Get("deckfile"); // Obsoleted
                    string dialog = queryParams.Get("dialog");
                    if (dialog != null)
                        Info.Dialog = dialog;
                    string version = queryParams.Get("version");
                    if (version != null)
                        Info.Version = Int16.Parse(version);
                    string password = queryParams.Get("password");
                    if (password != null)
                        Info.HostInfo = password;
                    string hand = queryParams.Get("hand");
                    if (hand != null)
                        Info.Hand = Int32.Parse(hand);
                    string debug = queryParams.Get("debug");
                    if (debug != null)
                        Info.Debug= bool.Parse(debug);
                    string chat = queryParams.Get("chat");
                    if (chat != null)
                        Info.Chat = bool.Parse(chat);

                    if (Info.Name == null || Info.Host == null || port == null || deckfile != null)
                    {
                        ctx.Response.StatusCode = 400;
                        ctx.Response.Close();
                    }
                    else if (Info.Deck != null && !DecksManager.HasDeck(Info.Deck))
                    {
                        ctx.Response.StatusCode = 404;
                        ctx.Response.Close();
                    }
                    else
                    {
                        ctx.Response.StatusCode = 200;
                        try
                        {
                            Thread workThread = new Thread(new ParameterizedThreadStart(Run));
                            workThread.Start(Info);
                        }
                        catch (Exception ex)
                        {
                            if (Debugger.IsAttached)
                                throw;
                            Logger.WriteErrorLine("Start Thread Error", ex);
                            ctx.Response.StatusCode = 500;
                        }
                        ctx.Response.Close();
                    }
                    }
                    catch (Exception ex)
                    {
                        if (Debugger.IsAttached)
                            throw;
                        Logger.WriteErrorLine("Parse Http Request Error", ex);
                    }
                }
            }
        }

        private static void Run(object o)
        {
            // All errors should be caught instead of causing the program to crash.
            GameClient client = null;
            try
            {
                WindBotInfo Info = (WindBotInfo)o;
                client = new GameClient(Info);
                Logger.SetContext(client.GetLogContext);
                client.Start();
                Logger.DebugWriteLine(client.Username + " started.");
                while (client.Connection.IsConnected)
                {
                    try
                    {
                        client.Tick();
                        #if DEBUG
                            Thread.Sleep(1);
                        #else
                            Thread.Sleep(30);
                        #endif
                    }
                    catch (Exception ex)
                    {
                        if (Debugger.IsAttached)
                            throw;
                        Logger.WriteErrorLine("Tick Error", ex);
                    }
                }
                Logger.DebugWriteLine(client.Username + " end.");
            }
            catch (Exception ex)
            {
                if (Debugger.IsAttached)
                    throw;
                Logger.WriteErrorLine("Run Error", ex);
            }
            finally
            {
                Logger.ClearContext();
            }
        }

        public static FileStream ReadFile(string directory, string filename, string extension)
        {
            if (ResourceRoot != null)
                return new FileStream(Path.Combine(ResourceRoot, directory, filename + "." + extension), FileMode.Open, FileAccess.Read, FileShare.Read);
            string tryfilename = filename + "." + extension;
            string fullpath = Path.Combine(directory, tryfilename);
            if (!File.Exists(fullpath))
                fullpath = filename;
            if (!File.Exists(fullpath))
                fullpath = Path.Combine("../", filename);
            if (!File.Exists(fullpath))
                fullpath = Path.Combine("../deck/", filename);
            if (!File.Exists(fullpath))
                fullpath = Path.Combine("../", tryfilename);
            if (!File.Exists(fullpath))
                fullpath = Path.Combine("../deck/", tryfilename);
#if UNDO_BUILD
            if(!File.Exists(fullpath)) Logger.WriteErrorLine("AI resource was not found: " + Path.GetFullPath(fullpath), null);
#endif
            return new FileStream(fullpath, FileMode.Open, FileAccess.Read);
        }
    }
}
