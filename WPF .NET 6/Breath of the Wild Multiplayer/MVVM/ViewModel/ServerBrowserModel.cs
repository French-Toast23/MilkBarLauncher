using Breath_of_the_Wild_Multiplayer.Source_files;
using Breath_of_the_Wild_Multiplayer.MVVM.Model;
using System;
using System.Collections.ObjectModel;
using System.Collections.Generic;
using System.IO;
using Newtonsoft.Json;
using System.Diagnostics;
using Breath_of_the_Wild_Multiplayer.MVVM.Model.DTO;
using System.Linq;
using System.Threading.Tasks;
using System.Text;
using System.Xml;

namespace Breath_of_the_Wild_Multiplayer.MVVM.ViewModel
{
    public class ServerBrowserModel : ObservableObject
    {
        private ObservableCollection<serverDataModel> _serversToShow = new ObservableCollection<serverDataModel>();

        public ObservableCollection<serverDataModel> serversToShow
        {
            get { return _serversToShow; }
            set
            {
                _serversToShow = value;
                OnPropertyChanged();
            }
        }

        private bool _isMaxOffset;

        public bool isMaxOffset
        {
            get { return _isMaxOffset; }
            set
            {
                _isMaxOffset = value;
                OnPropertyChanged();
            }
        }

        private int _scrollState;

        public int scrollState
        {
            get { return _scrollState; }
            set { 
                _scrollState = value;
                OnPropertyChanged();
            }
        }

        private ObservableCollection<serverDataModel> _selectedServer;

        public ObservableCollection<serverDataModel> selectedServer
        {
            get { return _selectedServer; }
            set { 
                _selectedServer = value;
                OnPropertyChanged();
            }
        }

        public bool IsModInstalled => ModInstaller.IsInstalled;

        private Dictionary<int, LocalServerDTO> ServerMapping;

        public List<LocalServerDTO> ServerList;

        public RelayCommand serverButtonClick { get; set; }
        public AsyncRelayCommand connectClick { get; set; }
        public RelayCommand refreshClick { get; set; }
        public RelayCommand addServerClick { get; set; }
        public RelayCommand directConnectClick { get; set; }

        public int lastSelected = -1;

        // ---- Cemu selector (above Connect): which of the up to four Cemu installs this launcher starts
        public RelayCommand previousCemuClick { get; set; }
        public RelayCommand nextCemuClick { get; set; }

        public string ActiveCemuLabel => CemuProfiles.ConfiguredSlots.Any()
            ? CemuProfiles.Describe(CemuProfiles.Active)
            : "No Cemu set (Settings)";

        public string ActiveCemuTooltip => (CemuProfiles.GetSlot(CemuProfiles.Active) ?? "Set your Cemu folders in Settings.") +
            "\nUse the arrows to pick which Cemu this launcher starts. Run one launcher per Cemu to play together.";

        public bool CanSwitchCemu => CemuProfiles.ConfiguredSlots.Count() > 1;

        public ServerBrowserModel()
        {
            SharedData.ServerBrowser = this;

            // Started by another launcher's "players on this PC" dialog: use the Cemu it chose for us.
            if (LaunchArgs.CemuSlot is int argSlot)
                CemuProfiles.SetActiveForThisProcess(argSlot);

            previousCemuClick = new RelayCommand(o => CemuProfiles.Cycle(-1));
            nextCemuClick = new RelayCommand(o => CemuProfiles.Cycle(1));
            CemuProfiles.Changed += () => System.Windows.Application.Current?.Dispatcher.BeginInvoke(new Action(() =>
            {
                OnPropertyChanged(nameof(ActiveCemuLabel));
                OnPropertyChanged(nameof(ActiveCemuTooltip));
                OnPropertyChanged(nameof(CanSwitchCemu));
                OnPropertyChanged(nameof(IsModInstalled));
                LoadServers();
            }));

            //findCemuData();
            isMaxOffset = true;
            scrollState = 0;

            LoadServers();

            serverButtonClick = new RelayCommand(o => this.changeSelected(o));
            connectClick = new AsyncRelayCommand(o => this.connectWithPlayerSetup());

            if (LaunchArgs.AutoConnect != null)
                System.Windows.Application.Current?.Dispatcher.BeginInvoke(System.Windows.Threading.DispatcherPriority.ApplicationIdle,
                    new Action(() => new AsyncRelayCommand(_ => this.autoConnect()).Execute(null)));
            refreshClick = new RelayCommand(o =>
            {
                foreach (var server in this._serversToShow)
                    _ = Task.Run(() => server.pingServer());
            });

            addServerClick = new RelayCommand(o =>
            {
                SharedData.MainView.updateTopView(new ServerEditorModel());
            });

            directConnectClick = new RelayCommand(o =>
            {
                var DirectConnectEditor = new ServerEditorModel();
                DirectConnectEditor.Setup(-2);
                SharedData.MainView.updateTopView(DirectConnectEditor);
            });
        }

        void changeSelected(object newSelection)
        {
            if(lastSelected != -1)
            {
                _serversToShow[lastSelected].selected = false;
            }

            _serversToShow[(int)newSelection].selected = true;
            _selectedServer[0] = _serversToShow[(int)newSelection];
            lastSelected = (int)newSelection;
        }

        public void LoadServers()
        {
            serverDataModel.serversAdded = 0;
            serversToShow.Clear();

            this.ServerList = JsonConvert.DeserializeObject<List<LocalServerDTO>>(Properties.Settings.Default.serversAdded)!;

            ServerMapping = new Dictionary<int, LocalServerDTO>();

            foreach (var server in this.ServerList.Where(sv => sv.Favorite))
            {
                serversToShow.Add(new serverDataModel(!string.IsNullOrEmpty(BcmlSettings.GameDir) && !string.IsNullOrEmpty(BcmlSettings.CemuDir), name: server.Name, ip: server.IP, port: server.Port, favorite: server.Favorite, password: server.Password));
                ServerMapping[serverDataModel.serversAdded - 1] = server;
            }

            foreach (var server in this.ServerList.Where(sv => !sv.Favorite))
            {
                serversToShow.Add(new serverDataModel(!string.IsNullOrEmpty(BcmlSettings.GameDir) && !string.IsNullOrEmpty(BcmlSettings.CemuDir), name: server.Name, ip: server.IP, port: server.Port, favorite: server.Favorite, password: server.Password));
                ServerMapping[serverDataModel.serversAdded - 1] = server;
            }

            selectedServer = new ObservableCollection<serverDataModel>();

            if (_serversToShow.Count == 0)
            {
                selectedServer.Add(new serverDataModel(!string.IsNullOrEmpty(BcmlSettings.GameDir) && !string.IsNullOrEmpty(BcmlSettings.CemuDir), name: "", ip: "", port: 0, favorite: false, password: ""));
            }
            else
            {
                selectedServer.Add(_serversToShow[0]);
                serversToShow[0].selected = true;
                lastSelected = 0;
            }
        }

        //void findCemuData()
        //{
        //    try
        //    {
        //        string CemuSettings = File.ReadAllText(Properties.Settings.Default.bcmlLocation);
        //        Dictionary<string, string> settings = JsonConvert.DeserializeObject<Dictionary<string, string>>(CemuSettings)!;
        //        CemuDir = settings["cemu_dir"];
        //        GameDir = settings["game_dir"];
        //    }
        //    catch(Exception ex)
        //    {
        //        CemuDir = "";
        //        GameDir = "";
        //    }
        //}

        /// <summary>
        /// Connect button: asks how many players play on this PC and which Cemu each one uses. Player 1 plays
        /// in this launcher; each other player gets a launcher of its own that connects to the same server.
        /// </summary>
        async Task connectWithPlayerSetup()
        {
            serverDataModel server = _selectedServer[0];
            if (string.IsNullOrEmpty(server.IP) || !server.open)
            {
                await connectToServer(null); // keeps the usual checks and messages
                return;
            }

            var dialog = new PlayerSetupWindow(server.Name) { Owner = System.Windows.Application.Current?.MainWindow };
            if (dialog.ShowDialog() != true)
                return;

            List<int> slots = dialog.ChosenSlots;
            CemuProfiles.Active = slots[0];

            var target = new LaunchArgs.ServerTarget { Name = server.Name, IP = server.IP, Port = server.Port, Password = server.Password };
            for (int i = 1; i < slots.Count; i++)
            {
                LaunchArgs.StartLauncherFor(slots[i], target);
                await Task.Delay(2000); // stagger the Cemu starts a little
            }

            await connectToServer(null);
        }

        /// <summary>Launcher started by another launcher's player dialog: connect right away.</summary>
        async Task autoConnect()
        {
            var t = LaunchArgs.AutoConnect;
            SharedData.SetLoadingMessage($"Connecting {CemuProfiles.Describe(CemuProfiles.Active)}...");
            var server = new serverDataModel(true, name: t.Name, ip: t.IP, port: t.Port, favorite: false, password: t.Password, async: false);
            if (!server.open)
                throw new ApplicationException("Could not connect to server. Check if the server is open.");
            await connectToServer(server);
        }

        async Task connectToServer(serverDataModel serverToJoin = null)
        {
            bool directConnection = serverToJoin != null;

            if (serverToJoin == null)
                serverToJoin = _selectedServer[0];

            if (string.IsNullOrEmpty(BcmlSettings.GameDir) || string.IsNullOrEmpty(BcmlSettings.CemuDir))
                throw new ApplicationException($"Cemu or Game path not found for {CemuProfiles.Describe(CemuProfiles.Active)}. Please verify your game and Cemu installation.");

            // Two launchers on the same Cemu folder would share its settings, logs and saves.
            string chosenCemu = Path.GetFullPath(BcmlSettings.CemuDir).TrimEnd('\\');
            foreach (Process running in Process.GetProcessesByName("Cemu"))
            {
                try
                {
                    string runningDir = Path.GetDirectoryName(running.MainModule?.FileName ?? "");
                    if (!string.IsNullOrEmpty(runningDir) &&
                        string.Equals(Path.GetFullPath(runningDir).TrimEnd('\\'), chosenCemu, StringComparison.OrdinalIgnoreCase))
                        throw new ApplicationException($"{CemuProfiles.Describe(CemuProfiles.Active)} is already running. Pick another Cemu with the arrows above Connect.");
                }
                catch (ApplicationException) { throw; }
                catch { }
            }

            if (!IsModInstalled)
            {
                SharedData.SetLoadingMessage("Installing multiplayer mod...");
                List<string> installWarnings = await Task.Run(() => ModInstaller.Install(msg => SharedData.SetLoadingMessage(msg)));
                if (installWarnings.Any())
                    throw new ApplicationException(string.Join("\n\n", installWarnings));
            }
            else
            {
                await Task.Run(() => ModInstaller.EnsureEnabledInCemu());
                // Re-deploy if this launcher ships a newer ModPack. Must run before the per-connect
                // edits below (player model, Jugador attention), which modify the deployed files.
                SharedData.SetLoadingMessage("Checking mod files...");
                await Task.Run(() => BcmlSettings.ValidateAndFixGraphicPack());
            }

            string cemuSettingsPath = ModInstaller.SettingsXmlPath;

            // Does the settings file for cemu exist?
            if (!File.Exists(cemuSettingsPath))
                throw new ApplicationException("Cemu settings file doesn't exist. Make sure to have opened Cemu at least once and setup the graphic packs.");

            XmlDocument doc = new XmlDocument();
            doc.Load(cemuSettingsPath);

            XmlNode GraphicPacksNode = doc.DocumentElement.SelectSingleNode("/content/GraphicPack");

            var aliasesToValidate = new Dictionary<string, string[]>
            {
                { "  - Multiplayer Mod", new[] { "BreathOfTheWild_Multiplayer", "MilkBarLauncher" } },
                { "  - Extended Memory", new[] { "ExtendedMemory" } }
            };

            List<string> missingGraphicPacks = new List<string>();

            try
            {
                if (GraphicPacksNode != null)
                {
                    foreach (var pair in aliasesToValidate)
                    {
                        bool isActive = false;
                        foreach (XmlNode node in GraphicPacksNode.ChildNodes)
                        {
                            var attr = node.Attributes?.Cast<XmlAttribute>().FirstOrDefault(a => a.Name.Equals("filename", StringComparison.OrdinalIgnoreCase));
                            if (attr != null)
                            {
                                string val = attr.Value.Replace('/', '\\');
                                if (pair.Value.Any(alias => val.IndexOf(alias, StringComparison.OrdinalIgnoreCase) >= 0))
                                {
                                    var disabledAttr = node.Attributes?["disabled"];
                                    if (disabledAttr == null || !string.Equals(disabledAttr.Value, "true", StringComparison.OrdinalIgnoreCase))
                                    {
                                        isActive = true;
                                        break;
                                    }
                                }
                            }
                        }
                        if (!isActive)
                            missingGraphicPacks.Add(pair.Key);
                    }
                }
                else
                {
                    missingGraphicPacks.AddRange(aliasesToValidate.Keys);
                }
            }
            catch(Exception ex)
            {
                throw new ApplicationException("Failed to read cemu graphic packs settings. Please make sure that your cemu is setup correctly.");
            }

            if(missingGraphicPacks.Any())
                throw new ApplicationException("The following needed graphic packs are not selected on Cemu\n\n" + string.Join("\n", missingGraphicPacks) + "\n\nPlease make sure to select them before joining a server.");
            if (string.IsNullOrEmpty(serverToJoin.IP) || !serverToJoin.open)
                return;

            serverToJoin.pingServer();

            if (!serverToJoin.open)
            {
                if (directConnection)
                    throw new Exception("Could not connect to server. The server may not be open.");
                else
                    return;
            }

            CharacterModel charModel = JsonConvert.DeserializeObject<CharacterModel>(Properties.Settings.Default.playerModel);

            SharedData.SetLoadingMessage("Loading player data...");

            await Task.Run(() => GameFilesModifier.CreateModifiedModel());

            await Task.Run(() => GameFilesModifier.ChangeAttentionForJugadores(serverToJoin.playStyle != "Prop hunt"));

            await Task.Run(() => GameFilesModifier.CleanAnimations());

            if (!Properties.Settings.Default.playAsModel)
            {
                await Task.Run(() => GameFilesModifier.ModifyGameROMPlayerModel("Link", "Link", false));
            }
            else
            {
                switch (charModel.Type)
                {
                    case CharacterModel.ModelType.ArmorSync:
                        // TODO: Add implementation for other armors with armor sync
                        await Task.Run(() => GameFilesModifier.ModifyGameROMPlayerModel(charModel.Name, charModel.Name, false));
                        break;
                    case CharacterModel.ModelType.CustomModel:
                        // We found that the game is only capable of having NPCs instead of Link. Therefore, we only change Link's model if it is an NPC
                        if (charModel.Model.StartsWith("Npc_"))
                            await Task.Run(() => GameFilesModifier.ModifyGameROMPlayerModel(charModel.Model, charModel.Model, true));
                        else
                            await Task.Run(() => GameFilesModifier.ModifyGameROMPlayerModel("Link", "Link", false));
                        break;
                    case CharacterModel.ModelType.Mii:
                        // TODO: Miis are not implemented. We found that trying to change Link to a Mii, you get issues when trying to find certain things in memory.
                        // If you want to work on this, go to the InjectDLL project, into the file MemoryAccess/LocalInstance.h
                        // There you can find the scan method. There we are looking for all the necessary addresses in memory. The specific memories that we found have issues are Animation and IsEquipped.
                        await Task.Run(() => GameFilesModifier.ModifyGameROMPlayerModel("Link", "Link", false));
                        break;
                }
            }

            await Task.Delay(2000);

            List<Process> ProcessesToFilter = Injector.GetProcesses("Cemu");

            SharedData.SetLoadingMessage("Starting Cemu...");

            string cemuExe = Path.Combine(BcmlSettings.CemuDir, "Cemu.exe");
            if (!File.Exists(cemuExe))
                cemuExe = Path.Combine(BcmlSettings.CemuDir, "cemu.exe");

            string rpxFile = Path.Combine(BcmlSettings.GameDir.Replace("content", "code"), "U-King.rpx");

            var cemuStartInfo = new ProcessStartInfo
            {
                FileName = cemuExe,
                Arguments = $"-g \"{rpxFile}\"",
                WorkingDirectory = BcmlSettings.CemuDir
            };
            Process startedCemu = await Task.Run(() => Process.Start(cemuStartInfo));

            await Task.Delay(500);

            SharedData.SetLoadingMessage("Injecting Cemu...");

            Process CemuProcess = null;

            string dllPath = Path.Combine(AppContext.BaseDirectory, "Resources", "InjectDLL.dll");
            if (!File.Exists(dllPath))
                dllPath = Path.Combine(Directory.GetCurrentDirectory(), "Resources", "InjectDLL.dll");

            // Inject the Cemu this launcher started. Picking "any new Cemu process" let two launchers that start
            // their Cemus at the same time grab the same one.
            await Task.Run(() => {
                CemuProcess = startedCemu != null && !startedCemu.HasExited
                    ? Injector.InjectToSpecificProcess("Cemu", dllPath, startedCemu)
                    : Injector.Inject("Cemu", dllPath, ProcessesToFilter);
            });

            SharedData.SetLoadingMessage("Starting pipe...");

            try
            {
                await Task.Run(NamedPipes.StartServer);
            }
            catch (Exception ex)
            {
                CemuProcess.Kill();
                throw ex;
            }

            SharedData.SetLoadingMessage("Connecting to server...");

            string resolvedIp = serverDataModel.ResolveIP(serverToJoin.IP).ToString();

            List<byte> instruction = Encoding.UTF8.GetBytes($"!connect;{resolvedIp};{serverToJoin.Port};{serverToJoin.Password};{Properties.Settings.Default.playerName};{serverToJoin.Name};{(int)charModel.Type};").ToList();

            if ((int)charModel.Type < 2)
            {
                if(charModel.Model.StartsWith("Npc_"))
                {
                    instruction.AddRange(Encoding.UTF8.GetBytes($"{charModel.Model}:{charModel.Model}"));
                } 
                else
                {
                    instruction.AddRange(Encoding.UTF8.GetBytes($"{charModel.Model}:{charModel.Name}"));
                }
            }
            else
                instruction.AddRange(Encoding.UTF8.GetBytes(JsonConvert.SerializeObject(charModel.BumiiData)));

            instruction.AddRange(Encoding.UTF8.GetBytes(";[END]"));

            await Task.Run(() => {
                if (!NamedPipes.sendInstruction(instruction.ToArray()))
                {
                    CemuProcess.Kill();
                    throw new ApplicationException("Could not connect to server. Internal connection failed.");
                }

                Task.Delay(50);

                if (!NamedPipes.sendInstruction($"!startServerLoop"))
                {
                    CemuProcess.Kill();
                    throw new ApplicationException("Could not start server loop.");
                }
            });

            SharedData.SetLoadingMessage("Starting overlay...");

            await Task.Delay(2000);

            SharedData.MainView.currentView = SharedData.MainView.IngameMenuVM;
            SharedData.MainView.disableBackground = true;
            SharedData.MainView.Window.Topmost = true;

            CemuProcess.Refresh();
            int handleRetries = 0;
            while (CemuProcess.MainWindowHandle == IntPtr.Zero && !CemuProcess.HasExited && handleRetries < 20)
            {
                await Task.Delay(250);
                CemuProcess.Refresh();
                handleRetries++;
            }

            CemuFollower.Setup(CemuProcess);
            NamedPipes.StartListenThread();

            SharedData.SetLoadingMessage();
        }

        void SaveServerSettings()
        {
            Properties.Settings.Default.serversAdded = JsonConvert.SerializeObject(this.ServerList);
            SettingsSaver.SaveSafe();
        }

        public void ModifyServer(ServerInfoModel serverinfo)
        {
            if(serverinfo.serverIndex == -1)
            {
                var serverToAdd = new LocalServerDTO()
                {
                    Name = serverinfo.name,
                    IP = serverinfo.ip,
                    Port = Int32.Parse(serverinfo.port),
                    Password = serverinfo.password,
                    Favorite = false
                };

                this.ServerList.Add(serverToAdd);

                this.serversToShow.Add(new serverDataModel(!string.IsNullOrEmpty(BcmlSettings.GameDir) && !string.IsNullOrEmpty(BcmlSettings.CemuDir), name: serverinfo.name, ip: serverinfo.ip, port: Int32.Parse(serverinfo.port), favorite: false, password: serverinfo.password));
                this.ServerMapping[serverDataModel.serversAdded - 1] = serverToAdd;
                changeSelected(this._serversToShow.Count - 1);

                SaveServerSettings();
            }
            else if(serverinfo.serverIndex == -2)
            {
                serverDataModel serverToconnect = new serverDataModel(!string.IsNullOrEmpty(BcmlSettings.GameDir) && !string.IsNullOrEmpty(BcmlSettings.CemuDir), name: serverinfo.name, ip: serverinfo.ip, port: Int32.Parse(serverinfo.port), favorite: false, password: serverinfo.password, async: false);

                if (!serverToconnect.open)
                    throw new Exception("Could not connect to server. Check if the server is open.");

                this.connectToServer(serverToconnect);
            }
            else
            {
                this.ServerMapping[serverinfo.serverIndex].Name = serverinfo.name;
                this.ServerMapping[serverinfo.serverIndex].IP = serverinfo.ip;
                this.ServerMapping[serverinfo.serverIndex].Port = Int32.Parse(serverinfo.port);
                this.ServerMapping[serverinfo.serverIndex].Password = serverinfo.password;

                this.serversToShow[serverinfo.serverIndex].Name = serverinfo.name;
                this.serversToShow[serverinfo.serverIndex].IP = serverinfo.ip;
                this.serversToShow[serverinfo.serverIndex].Port = Int32.Parse(serverinfo.port);
                this.serversToShow[serverinfo.serverIndex].Password = serverinfo.password;

                SaveServerSettings();

                this.selectedServer[0].pingServer();
            }
        }

        public void EditServer()
        {
            var DirectConnectEditor = new ServerEditorModel();
            DirectConnectEditor.Setup(this.selectedServer[0].serverIndex, this.selectedServer[0].Name, this.selectedServer[0].IP, this.selectedServer[0].Port.ToString(), this.selectedServer[0].Password.ToString());
            SharedData.MainView.updateTopView(DirectConnectEditor);
        }

        public void RemoveServer()
        {
            this.ServerList.Remove(this.ServerMapping[this.selectedServer[0].serverIndex]);
            SaveServerSettings();
            LoadServers();
        }

        public void ChangeFavorite(int serverIndex)
        {
            this.ServerMapping[serverIndex].Favorite = this.serversToShow[serverIndex].favorite;

            SaveServerSettings();
        }
    }
}
