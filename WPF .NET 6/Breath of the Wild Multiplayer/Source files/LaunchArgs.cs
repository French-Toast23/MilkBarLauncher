using Newtonsoft.Json;
using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;

namespace Breath_of_the_Wild_Multiplayer.Source_files
{
    /// <summary>
    /// Command line of a launcher started by the "players on this PC" dialog:
    ///   --cemu-slot N          use Cemu slot N (1-4) in this launcher only
    ///   --connect &lt;base64&gt;   connect to this server right after startup (JSON, see <see cref="ServerTarget"/>)
    /// Each local player gets their own launcher because a launcher drives exactly one Cemu
    /// (one injected DLL, one pipe, one overlay window).
    /// </summary>
    public static class LaunchArgs
    {
        public class ServerTarget
        {
            public string Name;
            public string IP;
            public int Port;
            public string Password;
        }

        public static int? CemuSlot { get; private set; }
        public static ServerTarget AutoConnect { get; private set; }

        static LaunchArgs()
        {
            try
            {
                string[] args = Environment.GetCommandLineArgs();
                for (int i = 1; i < args.Length - 1; i++)
                {
                    if (args[i] == "--cemu-slot" && int.TryParse(args[i + 1], out int slot) && slot >= 1 && slot <= CemuProfiles.SlotCount)
                        CemuSlot = slot - 1;
                    else if (args[i] == "--connect")
                        AutoConnect = JsonConvert.DeserializeObject<ServerTarget>(Encoding.UTF8.GetString(Convert.FromBase64String(args[i + 1])));
                }
            }
            catch { }
        }

        /// <summary>Starts another launcher that uses <paramref name="slot"/> and connects to <paramref name="server"/>.</summary>
        public static void StartLauncherFor(int slot, ServerTarget server)
        {
            string exe = Process.GetCurrentProcess().MainModule?.FileName
                ?? Path.Combine(AppContext.BaseDirectory, "Milk Bar Launcher.exe");
            string payload = Convert.ToBase64String(Encoding.UTF8.GetBytes(JsonConvert.SerializeObject(server)));
            Process.Start(new ProcessStartInfo(exe)
            {
                Arguments = $"--cemu-slot {slot + 1} --connect {payload}",
                WorkingDirectory = AppContext.BaseDirectory,
                UseShellExecute = false
            });
        }
    }
}
