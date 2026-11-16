using Breath_of_the_Wild_Multiplayer.Source_files;
using System;
using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;
using System.Diagnostics;
using Breath_of_the_Wild_Multiplayer.MVVM.Model;
using System.Linq;
using System.Windows.Threading;
using Microsoft.Win32;

namespace Breath_of_the_Wild_Multiplayer.MVVM.ViewModel
{
    public class SettingsPanelModel : ObservableObject
    {
        private bool _enableCustomModels;

        public bool EnableCustomModels
        {
            get { return _enableCustomModels; }
            set { 
                _enableCustomModels = value;
                Properties.Settings.Default.playAsModel = value;
                SettingsSaver.SaveSafe();
                OnPropertyChanged();
            }
        }

        public RelayCommand backgroundLeftButton { get; set; }
        public RelayCommand backgroundRightButton { get; set; }
        public RelayCommand restartSettings { get; set; }

        private bool _backgroundMovingLeft;

        public bool backgroundMovingLeft
        {
            get { return _backgroundMovingLeft; }
            set { 
                _backgroundMovingLeft = value;
                OnPropertyChanged();
            }
        }

        private bool _backgroundMovingRight;

        public bool backgroundMovingRight
        {
            get { return _backgroundMovingRight; }
            set { 
                _backgroundMovingRight = value;
                OnPropertyChanged();
            }
        }

        public RelayCommand discordButton { get; set; }
        public RelayCommand selectCemuSlot { get; set; }
        public RelayCommand clearCemuSlot { get; set; }
        public RelayCommand installModButton { get; set; }
        public RelayCommand uninstallModButton { get; set; }

        private bool _modOperationRunning;

        public string ModStatus =>
            $"{CemuProfiles.Describe(CemuProfiles.Active)}: " +
            (ModInstaller.IsInstalled ? "installed (click to repair)" : "not installed (click to install)") +
            "\nPick the Cemu with the selector above the Connect button.";

        /// <summary>Label of the Install / Uninstall rows: which Cemu they act on.</summary>
        public string ModTargetLabel => $"Mod on Cemu {CemuProfiles.Active + 1}";

        /// <summary>Button text of the four Cemu rows (folder name, or a prompt).</summary>
        public string[] CemuSlotButtons => Enumerable.Range(0, CemuProfiles.SlotCount)
            .Select(i => CemuProfiles.GetSlotDir(i) is string d ? new DirectoryInfo(d).Name : "Select Cemu.exe").ToArray();

        /// <summary>Tooltips of the four Cemu rows (full path).</summary>
        public string[] CemuSlotTooltips => Enumerable.Range(0, CemuProfiles.SlotCount)
            .Select(i => CemuProfiles.GetSlot(i) is string p && !string.IsNullOrEmpty(p)
                ? p + "\nClick to change, right-click to clear."
                : "Click to select a Cemu.exe. Each extra Cemu should be a separate (portable) copy so saves, logs and caches don't clash.")
            .ToArray();

        private void RefreshCemuBindings()
        {
            OnPropertyChanged(nameof(CemuSlotButtons));
            OnPropertyChanged(nameof(CemuSlotTooltips));
            OnPropertyChanged(nameof(ModStatus));
            OnPropertyChanged(nameof(ModTargetLabel));
        }

        static private string BackgroundsPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Backgrounds");
        List<Background> Backgrounds = new List<Background>();
        int index = -1;

        public DispatcherTimer timer = new DispatcherTimer();

        public SettingsPanelModel()
        {
            SharedData.SettingsPanel = this;

            List<string> Extensions = new List<string>() { ".png", ".jpg" };

            Backgrounds.AddRange(this.FindBackgrounds(BackgroundsPath, Extensions));

            findCurrentIndex();

            backgroundMovingLeft = false;
            backgroundMovingRight = false;
            timer.Interval = TimeSpan.FromSeconds(20);
            timer.Tick += new EventHandler(timer_Tick);

            this.EnableCustomModels = Properties.Settings.Default.playAsModel;

            backgroundLeftButton = new RelayCommand(async o =>
            {
                if(index >= 0 && !backgroundMovingLeft)
                {
                    SharedData.MainMenu.changingBackground = true;
                    backgroundMovingLeft = true;
                    index--;
                    await Sleep(300);

                    if (index == -1)
                    {
                        Properties.Settings.Default.background = "Random";
                        Properties.Settings.Default.backgroundDir = "";
                        Properties.Settings.Default.backgroundExt = "";
                        setTimer(true);
                    }
                    else
                    {
                        Properties.Settings.Default.background = Backgrounds[index].Filename;
                        Properties.Settings.Default.backgroundDir = Backgrounds[index].Path;
                        Properties.Settings.Default.backgroundExt = Backgrounds[index].Extension;
                    }
                    updateBackground();
                    SettingsSaver.SaveSafe();

                    await Sleep(300);
                    backgroundMovingLeft = false;
                    SharedData.MainMenu.changingBackground = false;
                }
            });

            backgroundRightButton = new RelayCommand(async o =>
            {
                if(index < Backgrounds.Count - 1 && !backgroundMovingRight)
                {
                    SharedData.MainMenu.changingBackground = true;
                    backgroundMovingRight = true;
                    index++;

                    if (index != -1)
                        setTimer(false);

                    await Sleep(300);
                    Properties.Settings.Default.background = Backgrounds[index].Filename;
                    Properties.Settings.Default.backgroundDir = Backgrounds[index].Path;
                    Properties.Settings.Default.backgroundExt = Backgrounds[index].Extension;
                    updateBackground();
                    SettingsSaver.SaveSafe();

                    await Sleep(300);
                    backgroundMovingRight = false;
                    SharedData.MainMenu.changingBackground = false;
                }
            });

            restartSettings = new RelayCommand(o =>
            {
                Properties.Settings.Default.background = "Random";
                Properties.Settings.Default.playerName = "Link";
                index = 0;
                updateBackground();
                SettingsSaver.SaveSafe();
            });

            installModButton = new RelayCommand(async o => await RunModOperation(true));
            uninstallModButton = new RelayCommand(async o => await RunModOperation(false));

            discordButton = new RelayCommand(o => {
                Process.Start(new ProcessStartInfo("https://discord.gg/sqeKHhBJse") { UseShellExecute=true});
            });

            selectCemuSlot = new RelayCommand(async o =>
            {
                if (!int.TryParse(o?.ToString(), out int slot) || slot < 0 || slot >= CemuProfiles.SlotCount)
                    return;

                // Empty slot: offer to download and set up Cemu 2.6 automatically.
                if (!CemuProfiles.IsConfigured(slot))
                {
                    var choice = System.Windows.MessageBox.Show(
                        $"Cemu {slot + 1} is empty.\n\n" +
                        "Yes: download Cemu 2.6 and set it up automatically (portable copy; settings, controllers, graphic packs and saves copied from your other Cemu, game files shared).\n\n" +
                        "No: choose a Cemu.exe you already have.",
                        $"Cemu {slot + 1}", System.Windows.MessageBoxButton.YesNoCancel, System.Windows.MessageBoxImage.Question);
                    if (choice == System.Windows.MessageBoxResult.Cancel)
                        return;
                    if (choice == System.Windows.MessageBoxResult.Yes)
                    {
                        await DownloadCemuIntoSlot(slot);
                        return;
                    }
                }

                string initDir = CemuProfiles.GetSlotDir(slot)
                    ?? CemuProfiles.ConfiguredSlots.Select(CemuProfiles.GetSlotDir).FirstOrDefault()
                    ?? Environment.GetFolderPath(Environment.SpecialFolder.Desktop);

                OpenFileDialog openFileDialog = new OpenFileDialog()
                {
                    Title = $"Select Cemu.exe for Cemu {slot + 1}",
                    Filter = "Cemu Executable (Cemu.exe)|Cemu.exe;cemu.exe|All files (*.*)|*.*",
                    InitialDirectory = initDir
                };

                if (openFileDialog.ShowDialog() != true)
                    return;

                string chosenDir = Path.GetDirectoryName(openFileDialog.FileName);
                for (int i = 0; i < CemuProfiles.SlotCount; i++)
                {
                    if (i != slot && CemuProfiles.GetSlotDir(i) is string other &&
                        string.Equals(Path.GetFullPath(other).TrimEnd('\\'), Path.GetFullPath(chosenDir).TrimEnd('\\'), StringComparison.OrdinalIgnoreCase))
                    {
                        System.Windows.MessageBox.Show($"That Cemu is already Cemu {i + 1}. Each slot needs its own Cemu folder.",
                            "Milk Bar Launcher", System.Windows.MessageBoxButton.OK, System.Windows.MessageBoxImage.Warning);
                        return;
                    }
                }

                if (!CemuProfiles.IsPortable(chosenDir) || !File.Exists(Path.Combine(chosenDir, "portable", "settings.xml")))
                {
                    var answer = System.Windows.MessageBox.Show(
                        (CemuProfiles.IsPortable(chosenDir)
                            ? "This portable Cemu isn't set up yet."
                            : "This Cemu keeps its settings, saves, logs and caches in %APPDATA%\\Cemu, shared with every other non-portable Cemu, so two of them running at once overwrite each other's files.") +
                        "\n\nSet it up as its own portable copy? Missing settings, controllers, graphic packs and saves are copied from your other Cemu and the game files are shared. Nothing that already exists is overwritten.",
                        $"Cemu {slot + 1}", System.Windows.MessageBoxButton.YesNo, System.Windows.MessageBoxImage.Question);
                    if (answer == System.Windows.MessageBoxResult.Yes)
                    {
                        try
                        {
                            var notes = await Task.Run(() => CemuSetup.SetUpPortableAsync(chosenDir, ShowProgress));
                            SharedData.SetLoadingMessage();
                            ReportSetup(slot, notes, Path.Combine(chosenDir, "Cemu.exe"));
                        }
                        catch (Exception ex)
                        {
                            SharedData.SetLoadingMessage();
                            System.Windows.MessageBox.Show(ex.Message, $"Cemu {slot + 1}", System.Windows.MessageBoxButton.OK, System.Windows.MessageBoxImage.Error);
                        }
                    }
                }

                CemuProfiles.SetSlot(slot, openFileDialog.FileName);
                if (!CemuProfiles.IsConfigured(CemuProfiles.Active))
                    CemuProfiles.Active = slot;
            });

            clearCemuSlot = new RelayCommand(o =>
            {
                if (!int.TryParse(o?.ToString(), out int slot))
                    return;
                CemuProfiles.SetSlot(slot, null);
                if (slot == CemuProfiles.Active && CemuProfiles.ConfiguredSlots.Any())
                    CemuProfiles.Active = CemuProfiles.ConfiguredSlots.First();
            });

            CemuProfiles.Changed += () => System.Windows.Application.Current?.Dispatcher.BeginInvoke(new Action(() =>
            {
                RefreshCemuBindings();
                SharedData.ServerBrowser?.LoadServers();
            }));
        }

        private static void ShowProgress(string msg) =>
            System.Windows.Application.Current?.Dispatcher.BeginInvoke(new Action(() => SharedData.SetLoadingMessage(msg)));

        /// <summary>Downloads Cemu 2.6 into a folder the player picks and assigns it to the slot.</summary>
        private async Task DownloadCemuIntoSlot(int slot)
        {
            string suggested = CemuProfiles.ConfiguredSlots.Select(CemuProfiles.GetSlotDir)
                                   .Select(d => Directory.GetParent(d)?.FullName).FirstOrDefault(d => d != null)
                               ?? Environment.GetFolderPath(Environment.SpecialFolder.Desktop);
            var folderDialog = new OpenFolderDialog
            {
                Title = $"Choose where to put Cemu {slot + 1} (a \"Cemu_2.6 p{slot + 1}\" folder is created inside)",
                InitialDirectory = suggested
            };
            if (folderDialog.ShowDialog() != true)
                return;

            string target = Path.Combine(folderDialog.FolderName, $"Cemu_2.6 p{slot + 1}");
            for (int n = 2; File.Exists(Path.Combine(target, "Cemu.exe")); n++)
                target = Path.Combine(folderDialog.FolderName, $"Cemu_2.6 p{slot + 1} ({n})");

            try
            {
                var (exe, notes) = await Task.Run(() => CemuSetup.InstallAsync(target, ShowProgress));
                SharedData.SetLoadingMessage();
                CemuProfiles.SetSlot(slot, exe);
                if (!CemuProfiles.IsConfigured(CemuProfiles.Active))
                    CemuProfiles.Active = slot;
                ReportSetup(slot, notes, exe);
            }
            catch (Exception ex)
            {
                SharedData.SetLoadingMessage();
                System.Windows.MessageBox.Show("Couldn't set up Cemu: " + ex.Message, $"Cemu {slot + 1}",
                    System.Windows.MessageBoxButton.OK, System.Windows.MessageBoxImage.Error);
            }
        }

        /// <summary>Tells the player what was set up and opens Cemu when something still has to be done in it.</summary>
        private static void ReportSetup(int slot, List<string> notes, string cemuExe)
        {
            if (notes.Count == 0)
            {
                System.Windows.MessageBox.Show($"Cemu {slot + 1} is ready.", $"Cemu {slot + 1}",
                    System.Windows.MessageBoxButton.OK, System.Windows.MessageBoxImage.Information);
                return;
            }

            var open = System.Windows.MessageBox.Show(
                $"Cemu {slot + 1} is installed. Still to do in Cemu itself:\n\n- " + string.Join("\n- ", notes) + "\n\nOpen Cemu now?",
                $"Cemu {slot + 1}", System.Windows.MessageBoxButton.YesNo, System.Windows.MessageBoxImage.Information);
            if (open == System.Windows.MessageBoxResult.Yes && File.Exists(cemuExe))
                Process.Start(new ProcessStartInfo(cemuExe) { WorkingDirectory = Path.GetDirectoryName(cemuExe), UseShellExecute = false });
        }

        private async Task RunModOperation(bool install)
        {
            if (_modOperationRunning)
                return;

            if (!install)
            {
                var confirm = System.Windows.MessageBox.Show(
                    $"This removes the multiplayer graphic pack from {CemuProfiles.Describe(CemuProfiles.Active)} and restores its graphic pack settings to how they were before installing.\n\nYour game files and save data are not touched.\n\nUninstall the mod?",
                    "Uninstall multiplayer mod", System.Windows.MessageBoxButton.YesNo, System.Windows.MessageBoxImage.Question);
                if (confirm != System.Windows.MessageBoxResult.Yes)
                    return;
            }

            _modOperationRunning = true;
            try
            {
                List<string> warnings = new List<string>();
                if (install)
                    warnings = await Task.Run(() => ModInstaller.Install());
                else
                    await Task.Run(() => ModInstaller.Uninstall());

                string target = CemuProfiles.Describe(CemuProfiles.Active);
                string message = install ? $"Multiplayer mod installed on {target}." : $"Multiplayer mod uninstalled from {target}. Everything it changed has been restored.";
                if (warnings.Any())
                    message += "\n\n" + string.Join("\n\n", warnings);

                System.Windows.MessageBox.Show(message, "Milk Bar Launcher", System.Windows.MessageBoxButton.OK,
                    warnings.Any() ? System.Windows.MessageBoxImage.Warning : System.Windows.MessageBoxImage.Information);
            }
            catch (Exception ex)
            {
                System.Windows.MessageBox.Show(ex.Message, "Milk Bar Launcher", System.Windows.MessageBoxButton.OK, System.Windows.MessageBoxImage.Error);
            }
            finally
            {
                _modOperationRunning = false;
                RefreshCemuBindings();
                SharedData.ServerBrowser?.LoadServers();
            }
        }

        private async Task Sleep(int i)
        {
            await Task.Delay(i);
        }

        private void setTimer(bool on)
        {
            if (on)
                timer.Start();
            else
                timer.Stop();
        }

        void findCurrentIndex()
        {
            List<int> matches = Backgrounds.Select((bg, i) => new { Index = i, Value = bg })
                                                  .Where(bg => Properties.Settings.Default.background == bg.Value.Filename && Properties.Settings.Default.backgroundDir == bg.Value.Path && Properties.Settings.Default.backgroundExt == bg.Value.Extension)
                                                  .Select(bg => bg.Index).ToList();

            if(matches.Count == 0)
            {
                index = -1;
                Properties.Settings.Default.background = "Random";
                Properties.Settings.Default.backgroundDir = "";
                Properties.Settings.Default.backgroundExt = "";
                updateBackground();
                SettingsSaver.SaveSafe();
                return;
            }

            index = matches.First();
        }

        void updateBackground()
        {
            if (Properties.Settings.Default.background == "Random" && string.IsNullOrEmpty(Properties.Settings.Default.backgroundDir) && string.IsNullOrEmpty(Properties.Settings.Default.backgroundExt))
            {
                string currentBackground = Properties.Settings.Default.actualBackground;
                string newBackground = currentBackground;
                int retries = 0;
                Random randomGenerator = new Random();
                
                while(currentBackground == newBackground && retries < 10)
                {
                    Background randomBackground = Backgrounds[randomGenerator.Next(Backgrounds.Count)];
                    newBackground = $"{randomBackground.Path}\\{randomBackground.Filename}{randomBackground.Extension}";
                    retries++;
                }

                Properties.Settings.Default.actualBackground = newBackground;
                return;
            }

            if (!File.Exists($"{Properties.Settings.Default.backgroundDir}\\{Properties.Settings.Default.background}{Properties.Settings.Default.backgroundExt}"))
            {
                Properties.Settings.Default.background = "Random";
                Properties.Settings.Default.backgroundDir = "";
                Properties.Settings.Default.backgroundExt = "";
                Properties.Settings.Default.actualBackground = "\\Images\\mainWindowBackground.png";
                SettingsSaver.SaveSafe();
                index = -1;
                return;
            }

            Properties.Settings.Default.actualBackground = $"{Properties.Settings.Default.backgroundDir}\\{Properties.Settings.Default.background}{Properties.Settings.Default.backgroundExt}";
        }

        public IEnumerable<Background> FindBackgrounds(string path, List<string> extensions)
        {
            if (string.IsNullOrEmpty(path) || !Directory.Exists(path))
                return Enumerable.Empty<Background>();

            try
            {
                return Directory.EnumerateFiles(path, "*.*", SearchOption.AllDirectories)
                    .Where(file => extensions.IndexOf(Path.GetExtension(file)) >= 0)
                    .Select(file => new Background() { Path = Path.GetDirectoryName(file), Filename = Path.GetFileNameWithoutExtension(file), Extension = Path.GetExtension(file) });
            }
            catch
            {
                return Enumerable.Empty<Background>();
            }
        }

        private async void timer_Tick(object sender, EventArgs e) 
        {
            SharedData.MainMenu.changingBackground = true;
            await Sleep(300);
            updateBackground();
            SharedData.MainMenu.changingBackground = false;
        }
    }
}
