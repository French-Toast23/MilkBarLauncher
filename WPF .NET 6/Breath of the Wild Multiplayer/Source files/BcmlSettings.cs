using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Xml.Linq;

namespace Breath_of_the_Wild_Multiplayer.Source_files
{
    public static class BcmlSettings
    {
        private static readonly string[] KnownTitleIds = { "101c9500", "101c9400", "101c9300" };

        public static string GameDir => ResolveGameDir();
        public static string UpdateDir => ResolveUpdateDir();
        public static string CemuDir => ResolveCemuDir();
        public static string MergedDir => ResolveMergedDir();

        /// <summary>
        /// Folder of the Cemu this launcher window uses: the active slot of <see cref="CemuProfiles"/>.
        /// Auto-detection is only used while no slot is set at all.
        /// </summary>
        public static string ResolveCemuDir()
        {
            string slotDir = CemuProfiles.GetSlotDir(CemuProfiles.Active);
            if (!string.IsNullOrEmpty(slotDir))
                return slotDir;
            if (CemuProfiles.ConfiguredSlots.Any())
                return ""; // the chosen slot is empty or its folder is gone: don't silently use another Cemu
            return AutoDetectCemuDir();
        }

        /// <summary>Best guess at a Cemu folder when the player hasn't picked one.</summary>
        public static string AutoDetectCemuDir()
        {
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

            return "";
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

        /// <summary>mlc01 folder used by the Cemu whose user data folder is <paramref name="userDataDir"/>.</summary>
        public static string ResolveMlcDirFor(string userDataDir)
        {
            try
            {
                string xml = Path.Combine(userDataDir, "settings.xml");
                if (File.Exists(xml))
                {
                    string customMlc = XDocument.Load(xml).Root?.Element("mlc_path")?.Value?.Trim();
                    if (!string.IsNullOrEmpty(customMlc) && Directory.Exists(customMlc))
                        return customMlc;
                }
            }
            catch { }
            return Path.Combine(userDataDir, "mlc01");
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

            // 2. mlc01 inside Cemu's user data folder (AppData, or "portable" next to Cemu.exe)
            string userDataMlc = Path.Combine(ModInstaller.ResolveCemuUserDataDir(), "mlc01");
            if (Directory.Exists(userDataMlc))
                return userDataMlc;

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

        // Content folder of the mod's Cemu graphic pack (created by ModInstaller, no BCML needed)
        private static string ResolveMergedDir() => ModInstaller.PackContentDir;

        public static string GetCemuSettingsXmlPath() => ModInstaller.SettingsXmlPath;

        /// <summary>Re-syncs the installed graphic pack with the launcher's ModPack before launching.</summary>
        public static void ValidateAndFixGraphicPack()
        {
            try
            {
                ModInstaller.Repair();
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[ValidateAndFixGraphicPack] Error: {ex.Message}");
            }
        }
    }
}