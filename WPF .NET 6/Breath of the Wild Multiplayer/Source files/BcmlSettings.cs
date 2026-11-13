using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Xml.Linq;
using Newtonsoft.Json;
using CsYaz0;

namespace Breath_of_the_Wild_Multiplayer.Source_files
{
    public static class BcmlSettings
    {
        private static readonly string[] KnownTitleIds = { "101c9500", "101c9400", "101c9300" };

        public static string GameDir => ResolveGameDir();
        public static string UpdateDir => ResolveUpdateDir();
        public static string CemuDir => ResolveCemuDir();
        public static string MergedDir => ResolveMergedDir();

        public static string ResolveCemuDir()
        {
            // 1. From user setting (cemuLocation)
            try
            {
                string userCemu = Properties.Settings.Default.cemuLocation;
                if (!string.IsNullOrEmpty(userCemu))
                {
                    if (File.Exists(userCemu))
                    {
                        string dir = Path.GetDirectoryName(userCemu);
                        if (!string.IsNullOrEmpty(dir) && Directory.Exists(dir))
                            return dir;
                    }
                    else if (Directory.Exists(userCemu))
                    {
                        return userCemu;
                    }
                }
            }
            catch { }

            // 2. From running Cemu process
            try
            {
                var running = Process.GetProcessesByName("Cemu").FirstOrDefault();
                if (running != null)
                {
                    string procDir = Path.GetDirectoryName(running.MainModule?.FileName);
                    if (!string.IsNullOrEmpty(procDir) && Directory.Exists(procDir))
                        return procDir;
                }
            }
            catch { }

            // 3. From saved settings or legacy config
            string fromSetting = getSetting("cemu_dir");
            if (!string.IsNullOrEmpty(fromSetting) && Directory.Exists(fromSetting))
                return fromSetting;

            // 4. Probe Desktop, Downloads, and UserProfile for Cemu directories
            try
            {
                string desktop = Environment.GetFolderPath(Environment.SpecialFolder.Desktop);
                if (Directory.Exists(desktop))
                {
                    foreach (var d in Directory.GetDirectories(desktop, "*cemu*", SearchOption.TopDirectoryOnly))
                    {
                        if (File.Exists(Path.Combine(d, "Cemu.exe")) || File.Exists(Path.Combine(d, "cemu.exe")))
                            return d;
                    }
                }

                string downloads = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "Downloads");
                if (Directory.Exists(downloads))
                {
                    foreach (var d in Directory.GetDirectories(downloads, "*cemu*", SearchOption.TopDirectoryOnly))
                    {
                        if (File.Exists(Path.Combine(d, "Cemu.exe")) || File.Exists(Path.Combine(d, "cemu.exe")))
                            return d;
                    }
                }
            }
            catch { }

            // 5. Common installation paths on Windows
            string[] probePaths = {
                Path.Combine(AppContext.BaseDirectory, "Cemu"),
                Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Programs", "Cemu"),
                @"C:\Cemu",
                @"D:\Cemu"
            };

            foreach (var probe in probePaths)
            {
                if (Directory.Exists(probe) && (File.Exists(Path.Combine(probe, "Cemu.exe")) || File.Exists(Path.Combine(probe, "cemu.exe"))))
                    return probe;
            }

            // 6. Check if Cemu.exe is in PATH
            try
            {
                string pathEnv = Environment.GetEnvironmentVariable("PATH") ?? "";
                foreach (string p in pathEnv.Split(';', StringSplitOptions.RemoveEmptyEntries))
                {
                    if (Directory.Exists(p) && (File.Exists(Path.Combine(p, "Cemu.exe")) || File.Exists(Path.Combine(p, "cemu.exe"))))
                        return p;
                }
            }
            catch { }

            return fromSetting ?? "";
        }

        public static string GetCemuDataDir()
        {
            // Cemu 2.x standard data dir on Windows
            string appDataCemu = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Cemu");
            if (Directory.Exists(appDataCemu))
                return appDataCemu;

            string cemuDir = ResolveCemuDir();
            if (!string.IsNullOrEmpty(cemuDir) && Directory.Exists(cemuDir))
                return cemuDir;

            return appDataCemu;
        }

        public static string ResolveMlcDir()
        {
            // 1. Check Cemu settings.xml for custom <mlc_path>
            string settingsXml = GetCemuSettingsXmlPath();
            if (File.Exists(settingsXml))
            {
                try
                {
                    var doc = XDocument.Load(settingsXml);
                    string customMlc = doc.Root?.Element("mlc_path")?.Value?.Trim();
                    if (!string.IsNullOrEmpty(customMlc) && Directory.Exists(customMlc))
                        return customMlc;
                }
                catch { }
            }

            // 2. Standard Cemu 2.x mlc01 in AppData
            string appDataMlc = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Cemu", "mlc01");
            if (Directory.Exists(appDataMlc))
                return appDataMlc;

            // 3. Portable mlc01 next to Cemu directory
            string cemuDir = ResolveCemuDir();
            if (!string.IsNullOrEmpty(cemuDir))
            {
                string localMlc = Path.Combine(cemuDir, "mlc01");
                if (Directory.Exists(localMlc))
                    return localMlc;
            }

            return appDataMlc;
        }

        private static string ResolveGameDir()
        {
            // 0. From user setting (gameLocation)
            try
            {
                string userGame = Properties.Settings.Default.gameLocation;
                if (!string.IsNullOrEmpty(userGame))
                {
                    if (File.Exists(userGame))
                    {
                        string dir = Path.GetDirectoryName(userGame);
                        string parent = Directory.GetParent(dir)?.FullName;
                        if (dir.EndsWith("code", StringComparison.OrdinalIgnoreCase) && parent != null && Directory.Exists(Path.Combine(parent, "content")))
                            return Path.Combine(parent, "content");
                        if (Directory.Exists(Path.Combine(dir, "content")))
                            return Path.Combine(dir, "content");
                    }
                    else if (Directory.Exists(userGame))
                    {
                        if (userGame.EndsWith("content", StringComparison.OrdinalIgnoreCase))
                            return userGame;
                        if (Directory.Exists(Path.Combine(userGame, "content")))
                            return Path.Combine(userGame, "content");
                    }
                }
            }
            catch { }

            string fromSetting = getSetting("game_dir");
            if (!string.IsNullOrEmpty(fromSetting) && Directory.Exists(fromSetting))
                return fromSetting;

            // 1. Look in discovered MLC01 title folders across all regions (US: 101c9500, EU: 101c9400, JP: 101c9300)
            string mlcDir = ResolveMlcDir();
            if (Directory.Exists(mlcDir))
            {
                foreach (string titleId in KnownTitleIds)
                {
                    string candidate = Path.Combine(mlcDir, "usr", "title", "00050000", titleId, "content");
                    if (Directory.Exists(candidate))
                        return candidate;
                }
            }

            // 2. Inspect Cemu settings.xml GamePaths and RecentLaunchFiles
            string settingsXml = GetCemuSettingsXmlPath();
            if (File.Exists(settingsXml))
            {
                try
                {
                    var doc = XDocument.Load(settingsXml);
                    var candidates = doc.Descendants("Entry").Select(e => e.Value?.Trim()).Where(v => !string.IsNullOrEmpty(v));
                    foreach (var path in candidates)
                    {
                        string normalized = path.Replace('/', '\\');
                        if (normalized.Contains("00050000", StringComparison.OrdinalIgnoreCase))
                        {
                            string contentDir = normalized.EndsWith("content", StringComparison.OrdinalIgnoreCase) ? normalized : Path.Combine(normalized, "content");
                            if (Directory.Exists(contentDir))
                                return contentDir;
                        }
                    }
                }
                catch { }
            }

            return null;
        }

        private static string ResolveUpdateDir()
        {
            string fromSetting = getSetting("update_dir");
            if (!string.IsNullOrEmpty(fromSetting) && Directory.Exists(fromSetting))
                return fromSetting;

            // 1. Look in discovered MLC01 title folders across all regions (US: 101c9500, EU: 101c9400, JP: 101c9300)
            string mlcDir = ResolveMlcDir();
            if (Directory.Exists(mlcDir))
            {
                foreach (string titleId in KnownTitleIds)
                {
                    string candidate = Path.Combine(mlcDir, "usr", "title", "0005000e", titleId, "content");
                    if (Directory.Exists(candidate))
                        return candidate;
                }
            }

            // 2. Inspect Cemu settings.xml RecentLaunchFiles
            string settingsXml = GetCemuSettingsXmlPath();
            if (File.Exists(settingsXml))
            {
                try
                {
                    var doc = XDocument.Load(settingsXml);
                    var candidates = doc.Descendants("Entry").Select(e => e.Value?.Trim()).Where(v => !string.IsNullOrEmpty(v));
                    foreach (var path in candidates)
                    {
                        string normalized = path.Replace('/', '\\');
                        if (normalized.Contains("0005000e", StringComparison.OrdinalIgnoreCase))
                        {
                            string contentDir = normalized.EndsWith("content", StringComparison.OrdinalIgnoreCase) ? normalized : Path.Combine(normalized, "content");
                            if (Directory.Exists(contentDir))
                                return contentDir;
                        }
                    }
                }
                catch { }
            }

            return null;
        }

        private static string ResolveMergedDir()
        {
            // 1. Cemu 2.x standard graphic pack directory in AppData
            string cemuGraphicPack = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Cemu", "graphicPacks", "BreathOfTheWild_Multiplayer", "content");
            if (Directory.Exists(cemuGraphicPack))
                return cemuGraphicPack;

            // 2. Graphic pack directory next to Cemu.exe
            string cemuDir = ResolveCemuDir();
            if (!string.IsNullOrEmpty(cemuDir))
            {
                string localGraphicPack = Path.Combine(cemuDir, "graphicPacks", "BreathOfTheWild_Multiplayer", "content");
                if (Directory.Exists(localGraphicPack))
                    return localGraphicPack;
            }

            // 3. BCML store_dir (if legacy BCML is used)
            string storeDir = getSetting("store_dir");
            if (!string.IsNullOrEmpty(storeDir))
            {
                string merged = Path.Combine(storeDir, "merged", "content");
                if (Directory.Exists(merged))
                    return merged;
            }

            return cemuGraphicPack;
        }

        public static string GetCemuSettingsXmlPath()
        {
            string appDataXml = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Cemu", "settings.xml");
            if (File.Exists(appDataXml))
                return appDataXml;

            string cemuDir = ResolveCemuDir();
            if (!string.IsNullOrEmpty(cemuDir))
            {
                string localXml = Path.Combine(cemuDir, "settings.xml");
                if (File.Exists(localXml))
                    return localXml;
            }

            return appDataXml;
        }

        private static string getSetting(string setting)
        {
            if (string.IsNullOrEmpty(Properties.Settings.Default.bcmlLocation) || !File.Exists(Properties.Settings.Default.bcmlLocation))
                return null;

            try
            {
                string cemuSettings = File.ReadAllText(Properties.Settings.Default.bcmlLocation);
                var dict = JsonConvert.DeserializeObject<Dictionary<string, string>>(cemuSettings);
                if (dict != null && dict.TryGetValue(setting, out string val))
                    return val;
            }
            catch
            {
                // Ignore parse errors from invalid or empty settings files
            }
            return null;
        }
    
        public static void ValidateAndFixGraphicPack()
        {
            try
            {
                string merged = ResolveMergedDir();
                if (string.IsNullOrEmpty(merged) || !Directory.Exists(merged))
                    return;

                // Ensure patch_NoFreezingLoadingScreen.asm is disabled as it breaks fast travel and locks camera in Cemu 2.6
                string graphicPackDir = Directory.GetParent(merged)?.FullName;
                if (!string.IsNullOrEmpty(graphicPackDir))
                {
                    string badPatch = Path.Combine(graphicPackDir, "patch_NoFreezingLoadingScreen.asm");
                    if (File.Exists(badPatch))
                    {
                        try { File.Move(badPatch, badPatch + ".disabled", true); } catch { }
                    }
                }

                // Ensure corrupt 173-byte dummy GameRomCamera.sbactorpack is disabled so original game camera loads
                string cameraPack = Path.Combine(merged, "Actor", "Pack", "GameRomCamera.sbactorpack");
                if (File.Exists(cameraPack))
                {
                    var fi = new FileInfo(cameraPack);
                    if (fi.Length < 1000)
                    {
                        try { File.Move(cameraPack, cameraPack + ".disabled", true); } catch { }
                    }
                }

                // Ensure merged ActorInfo.product.sbyml with Jugador entries is present in graphic pack
                string actorInfoPath = Path.Combine(merged, "Actor", "ActorInfo.product.sbyml");
                bool needsCopy = !File.Exists(actorInfoPath);
                if (!needsCopy)
                {
                    try
                    {
                        var fi = new FileInfo(actorInfoPath);
                        if (fi.Length < 800000)
                            needsCopy = true;
                    }
                    catch { }
                }

                if (needsCopy)
                {
                    string backupActorInfo = Path.Combine(AppContext.BaseDirectory, "patches", "ActorInfo.product.sbyml");
                    if (!File.Exists(backupActorInfo))
                        backupActorInfo = Path.Combine(Directory.GetCurrentDirectory(), "patches", "ActorInfo.product.sbyml");
                    if (!File.Exists(backupActorInfo))
                        backupActorInfo = Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..", "patches", "ActorInfo.product.sbyml");
                    if (File.Exists(backupActorInfo))
                    {
                        try
                        {
                            Directory.CreateDirectory(Path.GetDirectoryName(actorInfoPath)!);
                            File.Copy(backupActorInfo, actorInfoPath, true);
                        }
                        catch { }
                    }
                }
                string packDir = Path.Combine(merged, "Pack");
                if (!Directory.Exists(packDir))
                    return;

                string[] rawPacks = { "Bootup.pack", "TitleBG.pack" };
                foreach (string packName in rawPacks)
                {
                    string packPath = Path.Combine(packDir, packName);
                    if (File.Exists(packPath))
                    {
                        byte[] header = new byte[4];
                        byte[] decompressed = null;
                        using (var fs = new FileStream(packPath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
                        {
                            if (fs.Read(header, 0, 4) == 4 &&
                                header[0] == 0x59 && header[1] == 0x61 && header[2] == 0x7A && header[3] == 0x30)
                            {
                                fs.Seek(0, SeekOrigin.Begin);
                                byte[] compressed = new byte[fs.Length];
                                fs.Read(compressed, 0, compressed.Length);
                                decompressed = Yaz0.Decompress(compressed);
                            }
                        }
                        if (decompressed != null)
                        {
                            File.WriteAllBytes(packPath, decompressed);
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[ValidateAndFixGraphicPack] Error: {ex.Message}");
            }
        }
    }
}