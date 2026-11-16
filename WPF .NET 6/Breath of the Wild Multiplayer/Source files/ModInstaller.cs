using CsYaz0;
using Newtonsoft.Json;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Xml.Linq;

namespace Breath_of_the_Wild_Multiplayer.Source_files
{
    /// <summary>
    /// Installs the multiplayer mod as a self-contained Cemu graphic pack (no BCML/UKMM), and
    /// reverts everything it changed on uninstall.
    ///
    /// The game dump (mlc01 / game folders) is never written to. Install only touches:
    ///  - &lt;Cemu user data&gt;/graphicPacks/BreathOfTheWild_Multiplayer  (created, or a pre-existing one is backed up)
    ///  - &lt;Cemu user data&gt;/settings.xml  (graphic pack entries; backed up first)
    /// Every change is recorded in %APPDATA%/BOTWM/install_manifest.json so Uninstall can undo it exactly.
    /// </summary>
    public static class ModInstaller
    {
        public const string PackName = "BreathOfTheWild_Multiplayer";
        private const string MarkerFileName = ".milkbar_installed";
        private const string ExtendedMemoryRelative = "graphicPacks/downloadedGraphicPacks/BreathOfTheWild/Mods/ExtendedMemory/rules.txt";

        private static string BotwmDir => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "BOTWM");
        private static string ManifestPath => Path.Combine(BotwmDir, $"install_manifest{ProfileSuffix}.json");
        private static string BackupRoot => Path.Combine(BotwmDir, $"Backups{ProfileSuffix}");

        private static string DefaultUserDataDir => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Cemu");

        /// <summary>
        /// Every Cemu user data folder gets its own manifest and backups, so each Cemu slot can be installed
        /// and uninstalled on its own. The standard %APPDATA%/Cemu keeps the original names.
        /// </summary>
        private static string ProfileSuffix
        {
            get
            {
                string userData = ResolveCemuUserDataDir();
                if (SamePath(Path.GetFullPath(userData), Path.GetFullPath(DefaultUserDataDir)))
                    return "";
                using var sha = System.Security.Cryptography.SHA1.Create();
                byte[] hash = sha.ComputeHash(System.Text.Encoding.UTF8.GetBytes(Path.GetFullPath(userData).TrimEnd('\\', '/').ToLowerInvariant()));
                return "_" + Convert.ToHexString(hash).Substring(0, 8);
            }
        }

        /// <summary>User data folder a Cemu installed in <paramref name="cemuDir"/> uses.</summary>
        private static string UserDataDirFor(string cemuDir)
        {
            if (!string.IsNullOrEmpty(cemuDir))
            {
                string portable = Path.Combine(cemuDir, "portable");
                if (Directory.Exists(portable))
                    return portable;
            }
            if (Directory.Exists(DefaultUserDataDir))
                return DefaultUserDataDir;
            if (!string.IsNullOrEmpty(cemuDir) && File.Exists(Path.Combine(cemuDir, "settings.xml")))
                return cemuDir;
            return DefaultUserDataDir;
        }

        /// <summary>True when a running Cemu uses the same user data folder as the active slot.</summary>
        private static bool IsActiveCemuRunning()
        {
            string target = Path.GetFullPath(ResolveCemuUserDataDir());
            foreach (Process p in Process.GetProcessesByName("Cemu"))
            {
                try
                {
                    string exe = p.MainModule?.FileName;
                    if (string.IsNullOrEmpty(exe))
                        return true; // can't tell which Cemu it is: be safe
                    if (SamePath(Path.GetFullPath(UserDataDirFor(Path.GetDirectoryName(exe))), target))
                        return true;
                }
                catch
                {
                    return true;
                }
            }
            return false;
        }

        public class InstallManifest
        {
            public int Version = 1;
            public DateTime InstalledAtUtc;
            public string CemuUserDataDir;
            public string PackDir;
            public string PreviousPackBackup;              // a pre-existing pack folder we moved away, restored on uninstall
            public string SettingsXmlPath;
            public string SettingsXmlBackup;               // untouched copy taken before our first edit
            public List<string> AddedSettingsEntries = new List<string>();
            public List<string> ReenabledSettingsEntries = new List<string>();
            public List<FileBackup> FileBackups = new List<FileBackup>();
        }

        public class FileBackup
        {
            public string Original;
            public string Backup;
            public bool Existed;
        }

        // ------------------------------------------------------------------ locations

        /// <summary>Cemu's user data folder (holds settings.xml, graphicPacks, usually mlc01).</summary>
        public static string ResolveCemuUserDataDir() => UserDataDirFor(BcmlSettings.ResolveCemuDir());

        public static string PackDir => Path.Combine(ResolveCemuUserDataDir(), "graphicPacks", PackName);
        public static string PackContentDir => Path.Combine(PackDir, "content");
        public static string SettingsXmlPath => Path.Combine(ResolveCemuUserDataDir(), "settings.xml");

        /// <summary>The ModPack folder shipped with the launcher (or found in the repo when running from source).</summary>
        public static string FindModPackSource()
        {
            var starts = new[] { AppContext.BaseDirectory, Directory.GetCurrentDirectory() };
            foreach (string start in starts)
            {
                DirectoryInfo dir = new DirectoryInfo(start);
                for (int i = 0; dir != null && i < 6; i++, dir = dir.Parent)
                {
                    string candidate = Path.Combine(dir.FullName, "ModPack");
                    if (File.Exists(Path.Combine(candidate, "rules.txt")))
                        return candidate;
                }
            }
            return null;
        }

        public static bool IsInstalled =>
            File.Exists(Path.Combine(PackDir, MarkerFileName)) &&
            File.Exists(Path.Combine(PackDir, "rules.txt")) &&
            File.Exists(Path.Combine(PackContentDir, "Pack", "Bootup.pack")) &&
            File.Exists(Path.Combine(PackContentDir, "Pack", "TitleBG.pack"));

        public static InstallManifest LoadManifest()
        {
            try
            {
                if (File.Exists(ManifestPath))
                    return JsonConvert.DeserializeObject<InstallManifest>(File.ReadAllText(ManifestPath));
            }
            catch { }
            return null;
        }

        private static void SaveManifest(InstallManifest manifest)
        {
            Directory.CreateDirectory(BotwmDir);
            File.WriteAllText(ManifestPath, JsonConvert.SerializeObject(manifest, Formatting.Indented));
        }

        private static void EnsureCemuClosed()
        {
            if (IsActiveCemuRunning())
                throw new ApplicationException($"Please close {CemuProfiles.Describe(CemuProfiles.Active)} first. Cemu rewrites settings.xml when it exits, which would undo the changes.");
        }

        // ------------------------------------------------------------------ install

        /// <summary>Installs (or reinstalls) the mod. Returns a list of warnings for the user (empty when all good).</summary>
        public static List<string> Install(Action<string> progress = null)
        {
            progress ??= _ => { };
            var warnings = new List<string>();

            EnsureCemuClosed();

            string source = FindModPackSource()
                ?? throw new ApplicationException("ModPack folder not found next to the launcher. Please reinstall the launcher.");

            string updateDir = BcmlSettings.UpdateDir;
            if (string.IsNullOrEmpty(updateDir) ||
                !File.Exists(Path.Combine(updateDir, "Pack", "Bootup.pack")) ||
                !File.Exists(Path.Combine(updateDir, "Pack", "TitleBG.pack")))
                throw new ApplicationException("Could not find the Breath of the Wild v208 update (Pack/Bootup.pack). Install the game update in Cemu and try again.");

            InstallManifest manifest = LoadManifest() ?? new InstallManifest();
            manifest.InstalledAtUtc = DateTime.UtcNow;
            manifest.CemuUserDataDir = ResolveCemuUserDataDir();
            manifest.PackDir = PackDir;
            Directory.CreateDirectory(BackupRoot);

            // 1. Graphic pack folder: keep any pack we didn't create, so uninstall can put it back.
            progress("Preparing graphic pack folder...");
            if (Directory.Exists(PackDir))
            {
                if (!File.Exists(Path.Combine(PackDir, MarkerFileName)) && string.IsNullOrEmpty(manifest.PreviousPackBackup))
                {
                    string backup = Path.Combine(BackupRoot, $"{PackName}_{DateTime.Now:yyyyMMdd_HHmmss}");
                    MoveDirectory(PackDir, backup);
                    manifest.PreviousPackBackup = backup;
                }
                else
                {
                    Directory.Delete(PackDir, true);
                }
            }
            Directory.CreateDirectory(PackDir);
            SaveManifest(manifest); // record the backup before doing anything else

            // 2-3. Mod files + archives merged on top of the player's own game files
            DeployBundle(source, updateDir, progress);
            SaveManifest(manifest);

            // 4. Enable the packs in Cemu
            progress("Enabling graphic packs in Cemu...");
            warnings.AddRange(EnableInCemuSettings(manifest));
            SaveManifest(manifest);

            progress("Mod installed.");
            return warnings;
        }

        /// <summary>
        /// Copies the bundle, merges the archives and records which bundle version is installed.
        /// </summary>
        private static void DeployBundle(string source, string updateDir, Action<string> progress)
        {
            progress("Copying mod files...");
            CopyBundle(source, PackDir);

            progress("Merging Bootup.pack...");
            MergePack(Path.Combine(updateDir, "Pack", "Bootup.pack"), Path.Combine(source, "merge", "Bootup.pack"),
                      Path.Combine(PackContentDir, "Pack", "Bootup.pack"), deepMergeActorPacks: false);

            progress("Merging TitleBG.pack...");
            MergePack(Path.Combine(updateDir, "Pack", "TitleBG.pack"), Path.Combine(source, "merge", "TitleBG.pack"),
                      Path.Combine(PackContentDir, "Pack", "TitleBG.pack"), deepMergeActorPacks: true);

            string pauseMenu = Path.Combine(updateDir, "Actor", "Pack", "PauseMenuPlayer.sbactorpack");
            if (File.Exists(pauseMenu))
            {
                Directory.CreateDirectory(Path.Combine(PackContentDir, "Actor", "Pack"));
                File.Copy(pauseMenu, Path.Combine(PackContentDir, "Actor", "Pack", "PauseMenuPlayer.sbactorpack"), true);
            }

            File.WriteAllText(Path.Combine(PackDir, MarkerFileName),
                $"Installed by Milk Bar Launcher {DateTime.UtcNow:O}\nbundle={ComputeBundleHash(source)}");
        }

        /// <summary>Hash of every file in the ModPack bundle, so launcher updates are detected.</summary>
        private static string ComputeBundleHash(string source)
        {
            using var sha = System.Security.Cryptography.SHA256.Create();
            foreach (string file in Directory.EnumerateFiles(source, "*", SearchOption.AllDirectories)
                                             .OrderBy(f => f, StringComparer.OrdinalIgnoreCase))
            {
                byte[] name = System.Text.Encoding.UTF8.GetBytes(Path.GetRelativePath(source, file).Replace('\\', '/').ToLowerInvariant());
                sha.TransformBlock(name, 0, name.Length, null, 0);
                byte[] data = File.ReadAllBytes(file);
                sha.TransformBlock(data, 0, data.Length, null, 0);
            }
            sha.TransformFinalBlock(Array.Empty<byte>(), 0, 0);
            return Convert.ToHexString(sha.Hash!);
        }

        /// <summary>
        /// Run before each launch: when the launcher ships a different ModPack than the installed one
        /// (an update), re-deploys it; otherwise does nothing. Game files are still only read.
        /// </summary>
        public static void Repair()
        {
            string source = FindModPackSource();
            if (source == null || !IsInstalled)
                return;

            string marker = File.ReadAllText(Path.Combine(PackDir, MarkerFileName));
            string installed = marker.Split('\n').Select(l => l.Trim()).FirstOrDefault(l => l.StartsWith("bundle="))?.Substring(7);
            if (installed == ComputeBundleHash(source))
                return;

            string updateDir = BcmlSettings.UpdateDir;
            if (string.IsNullOrEmpty(updateDir) || !File.Exists(Path.Combine(updateDir, "Pack", "Bootup.pack")))
                return;

            DeployBundle(source, updateDir, _ => { });
        }

        // ------------------------------------------------------------------ uninstall

        /// <summary>Removes the mod and restores everything recorded in the manifest.</summary>
        public static void Uninstall(Action<string> progress = null)
        {
            progress ??= _ => { };
            EnsureCemuClosed();

            InstallManifest manifest = LoadManifest();
            string packDir = manifest?.PackDir ?? PackDir;

            progress("Removing graphic pack...");
            if (Directory.Exists(packDir) && (manifest != null || File.Exists(Path.Combine(packDir, MarkerFileName))))
                Directory.Delete(packDir, true);

            if (manifest != null && !string.IsNullOrEmpty(manifest.PreviousPackBackup) && Directory.Exists(manifest.PreviousPackBackup))
            {
                progress("Restoring previous graphic pack...");
                MoveDirectory(manifest.PreviousPackBackup, packDir);
            }

            if (manifest != null)
            {
                progress("Restoring Cemu settings...");
                RevertCemuSettings(manifest);

                foreach (FileBackup fb in manifest.FileBackups)
                {
                    try
                    {
                        if (fb.Existed && File.Exists(fb.Backup))
                            File.Copy(fb.Backup, fb.Original, true);
                        else if (!fb.Existed && File.Exists(fb.Original))
                            File.Delete(fb.Original);
                    }
                    catch { }
                }
            }

            progress("Cleaning up...");
            TryDeleteDirectory(BackupRoot);
            TryDeleteDirectory(Path.Combine(BotwmDir, "Temp"));
            try { if (File.Exists(ManifestPath)) File.Delete(ManifestPath); } catch { }

            progress("Mod uninstalled.");
        }

        /// <summary>Re-enables our graphic packs in Cemu if the player turned them off. Returns warnings.</summary>
        public static List<string> EnsureEnabledInCemu()
        {
            if (IsActiveCemuRunning())
                return new List<string>();
            InstallManifest manifest = LoadManifest() ?? new InstallManifest { PackDir = PackDir, CemuUserDataDir = ResolveCemuUserDataDir() };
            Directory.CreateDirectory(BackupRoot);
            var warnings = EnableInCemuSettings(manifest);
            SaveManifest(manifest);
            return warnings;
        }

        // ------------------------------------------------------------------ Cemu settings.xml

        private static List<string> EnableInCemuSettings(InstallManifest manifest)
        {
            var warnings = new List<string>();
            string settingsPath = SettingsXmlPath;
            string userData = ResolveCemuUserDataDir();
            manifest.SettingsXmlPath = settingsPath;

            if (!File.Exists(settingsPath))
            {
                warnings.Add("Cemu settings.xml was not found. Open Cemu once, then enable \"Breath of the Wild Multiplayer\" and \"Extended Memory\" under Options > Graphic packs.");
                return warnings;
            }

            if (string.IsNullOrEmpty(manifest.SettingsXmlBackup) || !File.Exists(manifest.SettingsXmlBackup))
            {
                string backup = Path.Combine(BackupRoot, "settings.xml");
                File.Copy(settingsPath, backup, true);
                manifest.SettingsXmlBackup = backup;
                SaveManifest(manifest);
            }

            // settings.xml is edited as text so everything we don't own stays byte-for-byte as Cemu wrote it.
            string text = ReadSettingsText(settingsPath, out bool hasBom);

            bool changed = EnableEntry(ref text, $"graphicPacks/{PackName}/rules.txt", manifest, warnings);

            if (File.Exists(Path.Combine(userData, ExtendedMemoryRelative.Replace('/', Path.DirectorySeparatorChar))))
                changed |= EnableEntry(ref text, ExtendedMemoryRelative, manifest, warnings);
            else
                warnings.Add("The \"Extended Memory\" community graphic pack is missing. In Cemu open Options > Graphic packs, click \"Download latest community graphic packs\", then enable Mods > Extended Memory.");

            if (changed)
                WriteSettingsText(settingsPath, text, hasBom);
            return warnings;
        }

        private static string ReadSettingsText(string path, out bool hasBom)
        {
            byte[] bytes = File.ReadAllBytes(path);
            hasBom = bytes.Length >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF;
            return new System.Text.UTF8Encoding(false).GetString(bytes, hasBom ? 3 : 0, bytes.Length - (hasBom ? 3 : 0));
        }

        private static void WriteSettingsText(string path, string text, bool hasBom) =>
            File.WriteAllText(path, text, new System.Text.UTF8Encoding(hasBom));

        private static System.Text.RegularExpressions.Regex EntryTag(string filename) =>
            new System.Text.RegularExpressions.Regex(
                @"<Entry\s+filename=""" + System.Text.RegularExpressions.Regex.Escape(NormalizeSlashes(filename)) + @"""([^>]*?)(/?)>",
                System.Text.RegularExpressions.RegexOptions.IgnoreCase);

        /// <summary>Makes sure the entry exists and isn't disabled. Returns true if the text was modified.</summary>
        private static bool EnableEntry(ref string text, string filename, InstallManifest manifest, List<string> warnings)
        {
            var match = EntryTag(filename).Match(text);
            if (match.Success)
            {
                string attrs = match.Groups[1].Value;
                if (!System.Text.RegularExpressions.Regex.IsMatch(attrs, @"disabled\s*=\s*""true""", System.Text.RegularExpressions.RegexOptions.IgnoreCase))
                    return false;

                string newTag = System.Text.RegularExpressions.Regex.Replace(match.Value, @"\s+disabled\s*=\s*""true""", "", System.Text.RegularExpressions.RegexOptions.IgnoreCase);
                text = text.Substring(0, match.Index) + newTag + text.Substring(match.Index + match.Length);
                if (!manifest.ReenabledSettingsEntries.Any(e => SamePath(e, filename)))
                    manifest.ReenabledSettingsEntries.Add(filename);
                return true;
            }

            string nl = text.Contains("\r\n") ? "\r\n" : "\n";
            string entryLine = $"<Entry filename=\"{filename}\"/>";
            int close = text.IndexOf("</GraphicPack>", StringComparison.Ordinal);
            var emptyNode = System.Text.RegularExpressions.Regex.Match(text, @"<GraphicPack\s*/>");
            int contentClose = text.LastIndexOf("</content>", StringComparison.Ordinal);

            if (close >= 0)
            {
                int lineStart = text.LastIndexOf('\n', close) + 1;
                string indent = text.Substring(lineStart, close - lineStart);
                if (indent.Trim().Length != 0) { indent = "    "; lineStart = close; }
                text = text.Insert(lineStart, indent + "    " + entryLine + nl);
            }
            else if (emptyNode.Success)
            {
                text = text.Substring(0, emptyNode.Index) + "<GraphicPack>" + nl + "        " + entryLine + nl + "    </GraphicPack>" + text.Substring(emptyNode.Index + emptyNode.Length);
            }
            else if (contentClose >= 0)
            {
                text = text.Insert(contentClose, "    <GraphicPack>" + nl + "        " + entryLine + nl + "    </GraphicPack>" + nl);
            }
            else
            {
                warnings.Add($"Could not update Cemu settings.xml. Enable \"{filename}\" manually under Options > Graphic packs.");
                return false;
            }

            if (!manifest.AddedSettingsEntries.Any(e => SamePath(e, filename)))
                manifest.AddedSettingsEntries.Add(filename);
            return true;
        }

        private static void RevertCemuSettings(InstallManifest manifest)
        {
            string settingsPath = manifest.SettingsXmlPath;
            if (string.IsNullOrEmpty(settingsPath) || !File.Exists(settingsPath))
                return;

            // Undo only our own edits so settings the user changed since install are kept.
            if (manifest.AddedSettingsEntries.Count == 0 && manifest.ReenabledSettingsEntries.Count == 0)
                return; // we never changed settings.xml

            try
            {
                string text = ReadSettingsText(settingsPath, out bool hasBom);

                foreach (string added in manifest.AddedSettingsEntries)
                {
                    string esc = System.Text.RegularExpressions.Regex.Escape(NormalizeSlashes(added));
                    // self-closing form, or the block form Cemu writes once presets are saved
                    var line = new System.Text.RegularExpressions.Regex(
                        @"[ \t]*<Entry\s+filename=""" + esc + @"""[^>]*/>[ \t]*(\r?\n)?|[ \t]*<Entry\s+filename=""" + esc + @"""[^>]*>.*?</Entry>[ \t]*(\r?\n)?",
                        System.Text.RegularExpressions.RegexOptions.IgnoreCase | System.Text.RegularExpressions.RegexOptions.Singleline);
                    text = line.Replace(text, "", 1);
                }

                foreach (string reenabled in manifest.ReenabledSettingsEntries)
                {
                    var match = EntryTag(reenabled).Match(text);
                    if (match.Success && !match.Groups[1].Value.Contains("disabled", StringComparison.OrdinalIgnoreCase))
                    {
                        int insertAt = match.Index + match.Value.IndexOf('"', match.Value.IndexOf('"') + 1) + 1;
                        text = text.Insert(insertAt, " disabled=\"true\"");
                    }
                }

                WriteSettingsText(settingsPath, text, hasBom);
            }
            catch
            {
                // settings.xml unreadable: fall back to the untouched copy taken at install
                if (!string.IsNullOrEmpty(manifest.SettingsXmlBackup) && File.Exists(manifest.SettingsXmlBackup))
                    File.Copy(manifest.SettingsXmlBackup, settingsPath, true);
            }
        }

        private static string NormalizeSlashes(string path) => path.Replace(Path.DirectorySeparatorChar, '/');

        private static bool SamePath(string a, string b) =>
            a != null && b != null &&
            string.Equals(a.Replace('\\', '/').Trim(), b.Replace('\\', '/').Trim(), StringComparison.OrdinalIgnoreCase);

        // ------------------------------------------------------------------ archives

        /// <summary>Writes basePack (player's game file) with every entry of modPack layered on top.</summary>
        private static void MergePack(string basePack, string modPack, string outputPath, bool deepMergeActorPacks)
        {
            var baseSarc = SarcLibrary.Sarc.FromBinary(ReadMaybeYaz0(basePack));
            var modSarc = SarcLibrary.Sarc.FromBinary(ReadMaybeYaz0(modPack));

            foreach (var kvp in modSarc)
            {
                if (kvp.Key == "Layout/Common.sblarc")
                    continue; // language/layout specific, keep the player's own

                if (deepMergeActorPacks && kvp.Key.EndsWith(".sbactorpack", StringComparison.OrdinalIgnoreCase) && baseSarc.ContainsKey(kvp.Key))
                {
                    // Only replace the inner files the mod ships, keep the rest of the vanilla actor
                    var baseActor = SarcLibrary.Sarc.FromBinary(DecompressIfYaz0(baseSarc[kvp.Key].ToArray()));
                    var modActor = SarcLibrary.Sarc.FromBinary(DecompressIfYaz0(kvp.Value.ToArray()));
                    foreach (var inner in modActor)
                        baseActor[inner.Key] = inner.Value;

                    using MemoryStream msActor = new();
                    baseActor.Write(msActor);
                    baseSarc[kvp.Key] = new ArraySegment<byte>(Yaz0.Compress(msActor.ToArray()).ToArray());
                }
                else
                {
                    baseSarc[kvp.Key] = kvp.Value;
                }
            }

            Directory.CreateDirectory(Path.GetDirectoryName(outputPath)!);
            using MemoryStream ms = new();
            baseSarc.Write(ms);
            File.WriteAllBytes(outputPath, ms.ToArray()); // .pack files are plain SARC
        }

        private static byte[] ReadMaybeYaz0(string path) => DecompressIfYaz0(File.ReadAllBytes(path));

        private static byte[] DecompressIfYaz0(byte[] data)
        {
            if (data.Length >= 4 && data[0] == 'Y' && data[1] == 'a' && data[2] == 'z' && data[3] == '0')
                return Yaz0.Decompress(data);
            return data;
        }

        // ------------------------------------------------------------------ file helpers

        private static IEnumerable<string> EnumerateBundleFiles(string source)
        {
            string mergeDir = Path.Combine(source, "merge") + Path.DirectorySeparatorChar;
            return Directory.EnumerateFiles(source, "*", SearchOption.AllDirectories)
                .Where(f => !f.StartsWith(mergeDir, StringComparison.OrdinalIgnoreCase))
                .Where(f => !Path.GetFileName(f).Equals("README.md", StringComparison.OrdinalIgnoreCase));
        }

        private static void CopyBundle(string source, string destination)
        {
            foreach (string file in EnumerateBundleFiles(source))
            {
                string dst = Path.Combine(destination, Path.GetRelativePath(source, file));
                Directory.CreateDirectory(Path.GetDirectoryName(dst)!);
                File.Copy(file, dst, true);
            }
        }

        private static void MoveDirectory(string from, string to)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(to)!);
            try
            {
                Directory.Move(from, to); // same volume: instant
            }
            catch (IOException)
            {
                // different volume: copy then delete
                foreach (string file in Directory.EnumerateFiles(from, "*", SearchOption.AllDirectories))
                {
                    string dst = Path.Combine(to, Path.GetRelativePath(from, file));
                    Directory.CreateDirectory(Path.GetDirectoryName(dst)!);
                    File.Copy(file, dst, true);
                }
                Directory.Delete(from, true);
            }
        }

        private static void TryDeleteDirectory(string dir)
        {
            try { if (Directory.Exists(dir)) Directory.Delete(dir, true); } catch { }
        }
    }
}
