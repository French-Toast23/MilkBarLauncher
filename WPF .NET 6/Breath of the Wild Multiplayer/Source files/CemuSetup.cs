using Newtonsoft.Json.Linq;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net.Http;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace Breath_of_the_Wild_Multiplayer.Source_files
{
    /// <summary>
    /// One-click Cemu for a launcher slot: downloads the official Cemu 2.6 release, makes it portable (Cemu keeps
    /// everything in &lt;cemu&gt;/portable when that folder exists) and seeds it from a Cemu that is already set up:
    /// settings, controller and game profiles, community graphic packs (Extended Memory), shader cache, saves,
    /// and a link to the installed game / update / DLC. Anything that already exists is left alone, so this also
    /// completes a portable copy made by hand.
    /// </summary>
    public static class CemuSetup
    {
        private const string CemuReleaseApi = "https://api.github.com/repos/cemu-project/Cemu/releases/tags/v2.6";
        private const string CemuZipFallback = "https://github.com/cemu-project/Cemu/releases/download/v2.6/cemu-2.6-windows-x64.zip";
        private const string GraphicPacksApi = "https://api.github.com/repos/cemu-project/cemu_graphic_packs/releases/latest";

        private static HttpClient NewClient()
        {
            var http = new HttpClient { Timeout = TimeSpan.FromMinutes(10) };
            http.DefaultRequestHeaders.UserAgent.ParseAdd("MilkBarLauncher"); // GitHub's API requires a User-Agent
            return http;
        }

        /// <summary>Downloads Cemu 2.6 into <paramref name="targetDir"/> and sets it up. Returns the path of Cemu.exe.</summary>
        public static async Task<string> DownloadAndSetUpAsync(string targetDir, Action<string> progress)
        {
            progress ??= _ => { };
            if (File.Exists(Path.Combine(targetDir, "Cemu.exe")))
                throw new ApplicationException("That folder already contains a Cemu. Pick an empty folder.");

            string tempRoot = Path.Combine(Path.GetTempPath(), "MilkBarCemu_" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(tempRoot);
            try
            {
                using var http = NewClient();

                progress("Finding Cemu 2.6...");
                string zipUrl = CemuZipFallback;
                try
                {
                    var release = JObject.Parse(await http.GetStringAsync(CemuReleaseApi));
                    zipUrl = release["assets"]?.Select(a => (string)a["browser_download_url"])
                                 .FirstOrDefault(u => u != null && Regex.IsMatch(u, @"windows-x64\.zip$", RegexOptions.IgnoreCase))
                             ?? CemuZipFallback;
                }
                catch { /* API rate limit or offline: use the known URL */ }

                string zipPath = Path.Combine(tempRoot, "cemu.zip");
                await DownloadAsync(http, zipUrl, zipPath, pct => progress($"Downloading Cemu 2.6... {pct}%"));

                progress("Unpacking Cemu...");
                string unpacked = Path.Combine(tempRoot, "unpacked");
                ZipFile.ExtractToDirectory(zipPath, unpacked);
                string exe = Directory.EnumerateFiles(unpacked, "Cemu.exe", SearchOption.AllDirectories).FirstOrDefault()
                    ?? throw new ApplicationException("The Cemu download didn't contain Cemu.exe.");
                CopyDirectory(Path.GetDirectoryName(exe)!, targetDir, overwrite: false);
            }
            finally
            {
                try { Directory.Delete(tempRoot, true); } catch { }
            }

            Directory.CreateDirectory(Path.Combine(targetDir, "portable")); // portable from the very first start
            return Path.Combine(targetDir, "Cemu.exe"); // the caller runs SetUpPortableAsync next
        }

        /// <summary>Downloads Cemu 2.6 into <paramref name="targetDir"/> and sets it up; returns Cemu.exe and what's still missing.</summary>
        public static async Task<(string Exe, List<string> Notes)> InstallAsync(string targetDir, Action<string> progress)
        {
            string exe = await DownloadAndSetUpAsync(targetDir, progress);
            return (exe, await SetUpPortableAsync(targetDir, progress));
        }

        /// <summary>
        /// Makes <paramref name="cemuDir"/> portable and fills in whatever is missing from an existing Cemu.
        /// Returns notes for the user (empty when everything was found).
        /// </summary>
        public static async Task<List<string>> SetUpPortableAsync(string cemuDir, Action<string> progress)
        {
            progress ??= _ => { };
            var notes = new List<string>();
            string portable = Path.Combine(cemuDir, "portable");
            Directory.CreateDirectory(portable);

            string source = FindSourceUserData(cemuDir);
            progress("Copying settings...");

            if (!File.Exists(Path.Combine(portable, "settings.xml")))
            {
                string srcSettings = source == null ? null : Path.Combine(source, "settings.xml");
                if (srcSettings != null && File.Exists(srcSettings))
                {
                    string text = File.ReadAllText(srcSettings);
                    // our pack entry is added back (and later removed) by this copy's own install/uninstall
                    text = Regex.Replace(text,
                        @"[ \t]*<Entry\s+filename=""graphicPacks/" + ModInstaller.PackName + @"/rules\.txt""[^>]*/>[ \t]*\r?\n?|[ \t]*<Entry\s+filename=""graphicPacks/" + ModInstaller.PackName + @"/rules\.txt""[^>]*>.*?</Entry>[ \t]*\r?\n?",
                        "", RegexOptions.IgnoreCase | RegexOptions.Singleline);
                    // a custom mlc path would make every copy share saves
                    text = Regex.Replace(text, @"<mlc_path>[^<]*</mlc_path>", "<mlc_path></mlc_path>");
                    File.WriteAllText(Path.Combine(portable, "settings.xml"), text);
                }
            }

            if (source != null)
            {
                CopyDirectory(Path.Combine(source, "controllerProfiles"), Path.Combine(portable, "controllerProfiles"), overwrite: false);
                CopyDirectory(Path.Combine(source, "gameProfiles"), Path.Combine(portable, "gameProfiles"), overwrite: false);
                CopyDirectory(Path.Combine(source, "graphicPacks", "downloadedGraphicPacks"), Path.Combine(portable, "graphicPacks", "downloadedGraphicPacks"), overwrite: false);
                CopyDirectory(Path.Combine(source, "shaderCache", "transferable"), Path.Combine(portable, "shaderCache", "transferable"), overwrite: false);
            }

            if (!Directory.Exists(Path.Combine(portable, "graphicPacks", "downloadedGraphicPacks", "BreathOfTheWild")))
            {
                try { await DownloadCommunityGraphicPacksAsync(Path.Combine(portable, "graphicPacks", "downloadedGraphicPacks"), progress); }
                catch (Exception ex) { notes.Add("Couldn't download Cemu's community graphic packs (" + ex.Message + "). In Cemu: Options > Graphic packs > Download latest community graphic packs."); }
            }

            // Game files: link (not copy) the installed game, update and DLC; saves are copied so each Cemu has its own.
            string dstMlc = Path.Combine(portable, "mlc01");
            string srcMlc = source == null ? null : BcmlSettings.ResolveMlcDirFor(source);
            if (srcMlc != null && Directory.Exists(srcMlc))
            {
                progress("Copying saves...");
                CopyDirectory(Path.Combine(srcMlc, "sys"), Path.Combine(dstMlc, "sys"), overwrite: false);
                CopyDirectory(Path.Combine(srcMlc, "usr", "save"), Path.Combine(dstMlc, "usr", "save"), overwrite: false);
            }

            string dstTitle = Path.Combine(dstMlc, "usr", "title");
            string srcTitle = srcMlc == null ? null : ResolveLink(Path.Combine(srcMlc, "usr", "title"));
            bool hasTitles = Directory.Exists(dstTitle) && Directory.EnumerateFileSystemEntries(dstTitle).Any();
            if (!hasTitles && srcTitle != null && Directory.Exists(srcTitle) && Directory.EnumerateFileSystemEntries(srcTitle).Any())
            {
                progress("Linking game files...");
                if (Directory.Exists(dstTitle) && !Directory.EnumerateFileSystemEntries(dstTitle, "*", SearchOption.AllDirectories).Any(File.Exists))
                    Directory.Delete(dstTitle, true); // empty folder Cemu created on first start
                Directory.CreateDirectory(Path.Combine(dstMlc, "usr"));
                CreateJunction(dstTitle, srcTitle);
                hasTitles = true;
            }
            if (!hasTitles)
                notes.Add("No installed game found to share. Start this Cemu once and install Breath of the Wild, its v208 update and the DLC (File > Install game title, update or DLC), or open your .wua.");

            if (!Directory.Exists(Path.Combine(portable, "controllerProfiles")) || !Directory.EnumerateFiles(Path.Combine(portable, "controllerProfiles")).Any())
                notes.Add("No controller profile found to copy. When Cemu opens, set your controller in Options > Input settings, then close Cemu.");

            return notes;
        }

        /// <summary>Cemu's community graphic packs (they include Extended Memory, which the mod needs).</summary>
        private static async Task DownloadCommunityGraphicPacksAsync(string destination, Action<string> progress)
        {
            using var http = NewClient();
            progress("Finding Cemu's community graphic packs...");
            var release = JObject.Parse(await http.GetStringAsync(GraphicPacksApi));
            string url = release["assets"]?.Select(a => (string)a["browser_download_url"]).FirstOrDefault(u => u != null && u.EndsWith(".zip", StringComparison.OrdinalIgnoreCase))
                ?? throw new ApplicationException("no zip in the latest release");
            string tag = (string)release["tag_name"] ?? "";

            string temp = Path.Combine(Path.GetTempPath(), "MilkBarPacks_" + Guid.NewGuid().ToString("N") + ".zip");
            try
            {
                await DownloadAsync(http, url, temp, pct => progress($"Downloading community graphic packs... {pct}%"));
                Directory.CreateDirectory(destination);
                ZipFile.ExtractToDirectory(temp, destination, overwriteFiles: true);
                File.WriteAllText(Path.Combine(destination, "version.txt"), tag); // what Cemu's own downloader writes
            }
            finally
            {
                try { File.Delete(temp); } catch { }
            }
        }

        /// <summary>The user data folder of an already set-up Cemu to copy from (another launcher slot, or %APPDATA%\Cemu).</summary>
        private static string FindSourceUserData(string excludeCemuDir)
        {
            string exclude = Path.GetFullPath(excludeCemuDir).TrimEnd('\\');
            var candidates = new List<string>();
            foreach (int slot in CemuProfiles.ConfiguredSlots)
            {
                string dir = CemuProfiles.GetSlotDir(slot);
                if (dir == null || string.Equals(Path.GetFullPath(dir).TrimEnd('\\'), exclude, StringComparison.OrdinalIgnoreCase))
                    continue;
                candidates.Add(Directory.Exists(Path.Combine(dir, "portable")) ? Path.Combine(dir, "portable") : CemuProfiles.SharedUserDataDir);
            }
            candidates.Add(CemuProfiles.SharedUserDataDir);

            // prefer a Cemu that has the game installed, then any with a settings.xml
            return candidates.FirstOrDefault(c => File.Exists(Path.Combine(c, "settings.xml")) &&
                                                  Directory.Exists(ResolveLink(Path.Combine(BcmlSettings.ResolveMlcDirFor(c), "usr", "title")) ?? ""))
                ?? candidates.FirstOrDefault(c => File.Exists(Path.Combine(c, "settings.xml")));
        }

        /// <summary>Follows a junction / symlink to its final folder (so links don't chain through other copies).</summary>
        private static string ResolveLink(string path)
        {
            try
            {
                var info = new DirectoryInfo(path);
                if (!info.Exists) return null;
                return info.LinkTarget != null ? (info.ResolveLinkTarget(true)?.FullName ?? path) : path;
            }
            catch { return path; }
        }

        private static async Task DownloadAsync(HttpClient http, string url, string path, Action<int> percent)
        {
            using var response = await http.GetAsync(url, HttpCompletionOption.ResponseHeadersRead);
            response.EnsureSuccessStatusCode();
            long total = response.Content.Headers.ContentLength ?? -1;
            using var input = await response.Content.ReadAsStreamAsync();
            using var output = File.Create(path);
            var buffer = new byte[81920];
            long done = 0; int lastPct = -1, read;
            while ((read = await input.ReadAsync(buffer, 0, buffer.Length)) > 0)
            {
                await output.WriteAsync(buffer, 0, read);
                done += read;
                if (total > 0)
                {
                    int pct = (int)(done * 100 / total);
                    if (pct != lastPct) { lastPct = pct; percent(pct); }
                }
            }
        }

        private static void CopyDirectory(string from, string to, bool overwrite)
        {
            if (!Directory.Exists(from)) return;
            foreach (string file in Directory.EnumerateFiles(from, "*", SearchOption.AllDirectories))
            {
                string dst = Path.Combine(to, Path.GetRelativePath(from, file));
                if (!overwrite && File.Exists(dst)) continue;
                Directory.CreateDirectory(Path.GetDirectoryName(dst)!);
                File.Copy(file, dst, true);
            }
            Directory.CreateDirectory(to);
        }

        /// <summary>Directory junction (no admin rights needed, unlike symbolic links).</summary>
        private static void CreateJunction(string link, string target)
        {
            var psi = new ProcessStartInfo("cmd.exe", $"/c mklink /J \"{link}\" \"{target}\"")
            {
                CreateNoWindow = true,
                UseShellExecute = false,
                RedirectStandardError = true,
                RedirectStandardOutput = true
            };
            using var proc = Process.Start(psi)!;
            string err = proc.StandardError.ReadToEnd();
            proc.WaitForExit();
            if (proc.ExitCode != 0 || !Directory.Exists(link))
                throw new ApplicationException("Could not link the game files: " + err.Trim());
        }
    }
}
