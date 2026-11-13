using BOTWM.Server.DTO;
using BOTWM.Server.HelperTypes;
using BOTWM.Server.ServerClasses;
using Newtonsoft.Json;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Text;
using BOTWM.Server.JSONBuilder;
using System.Diagnostics;
using BOTW.Logging;

namespace BOTWM.Server
{
    public class Server
    {
        bool serverOpen = false;

        public string Version = "0.20.0";

        public short SerializationRate = 60;
        public short TargetFPS = 60;
        public short SleepMultiplier = 1;
        public bool isLocalTest = false;
        public bool ischaracterSpawn = true;
        public bool DisplayNames = true;
        public short GlyphDistance = 250;
        public short GlyphTime = 60;
        public bool isQuestSync = false;
        public bool isEnemySync = false;
        public string Gamemode = "";

        public bool EnemyLog { get; set; }

        public int ClientLog { get; set; }
        public bool ServerLog { get; set; }

        Socket listen;
        Thread listenThread;
        List<Thread> clientThreads = new List<Thread>();

        public bool serverStart(string ip, int port, string password, string description, ServerSettings settings)
        {
            this.Gamemode = settings.SettingsName;

            Dictionary<string, string> ipAddresses = new Dictionary<string, string>();

            try
            {
                var host = Dns.GetHostEntry(Dns.GetHostName());

                foreach (NetworkInterface item in NetworkInterface.GetAllNetworkInterfaces())
                {
                    if (item.OperationalStatus == OperationalStatus.Up)
                    {
                        foreach (UnicastIPAddressInformation uip in item.GetIPProperties().UnicastAddresses)
                        {
                            if (uip.Address.AddressFamily == AddressFamily.InterNetwork && host.AddressList.Contains(uip.Address))
                            {
                                if (!ipAddresses.ContainsKey(item.Name))
                                    ipAddresses.Add(item.Name, uip.Address.ToString());
                            }
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                Logger.LogDebug($"Could not enumerate network interfaces: {ex.Message}");
            }

            bool bindAll = string.IsNullOrWhiteSpace(ip) ||
                           ip.Equals("localhost", StringComparison.OrdinalIgnoreCase) ||
                           ip.Equals("0.0.0.0", StringComparison.OrdinalIgnoreCase) ||
                           ip.Equals("any", StringComparison.OrdinalIgnoreCase);

            if (bindAll)
            {
                try
                {
                    listen = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp);
                    IPEndPoint connect = new IPEndPoint(IPAddress.Any, port);
                    listen.Bind(connect);

                    Logger.LogInformation($"Server opened on port {port} (listening on all interfaces: 0.0.0.0).");
                    Logger.LogInformation("Available connection addresses for players:");
                    Logger.LogInformation($"  - Localhost (same PC): 127.0.0.1:{port}");
                    foreach (var key in ipAddresses.Keys)
                    {
                        Logger.LogInformation($"  - {key}: {ipAddresses[key]}:{port}");
                    }

                    ServerData.Startup("0.0.0.0", port, password, description, settings);

                    serverOpen = true;

                    return true;
                }
                catch (Exception ex)
                {
                    Logger.LogError($"Failed to bind to 0.0.0.0:{port}: {ex}");
                }
            }
            else
            {
                try
                {
                    listen = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp);
                    IPEndPoint connect = new IPEndPoint(IPAddress.Parse(ip), port);
                    listen.Bind(connect);

                    Logger.LogInformation($"Server opened on {ip}:{port}.");

                    ServerData.Startup(ip, port, password, description, settings);

                    serverOpen = true;

                    return true;
                }
                catch (Exception ex)
                {
                    Logger.LogError($"Failed to bind to custom IP {ip}:{port}: {ex}");
                }
            }

            return false;
        }

        public void stopServer()
        {
            serverOpen = false;
            listen.Close();

            if (listenThread != null)
                listenThread.Abort();

            foreach (Thread clientThread in clientThreads)
                clientThread.Abort();
        }

        public void startListen()
        {
            listenThread = new Thread(serverListen);
            listenThread.IsBackground = true;
            listenThread.Start();
        }

        public void serverListen()
        {
            while(true)
            {
                Socket connection;

                listen.Listen(100);

                connection = listen.Accept();

                var clientThread = new Thread(() => handleClient(connection));
                clientThread.Start();
                clientThreads.Add(clientThread);
            }
        }

        public void handleClient(Socket connection)
        {
            int SIZE = 10240;

            byte[] info = new byte[SIZE];
            List<byte> data = new List<byte>();
            int totalLength = 0;
            int retries = 0;
            Stopwatch RetryWatch = new Stopwatch();

            bool ClientConnected = true;
            int PlayerNumber = -1;
            string PlayerName = "";
            
            while(serverOpen && ClientConnected)
            {
                try
                {
                    if (retries == 0)
                        RetryWatch.Restart();

                    totalLength += connection.Receive(info, 0, info.Length, 0);

                    data.AddRange(info.ToList());

                    if (totalLength < 6144)
                    {
                        retries++;

                        if (retries > 10 && totalLength == 0)
                            throw new ApplicationException("Connection lost with player.");

                        continue;
                    }

                    Tuple<MessageType, object> ClientMessage = new JSONBuilder.JSONBuilder().BuildFromBytes(data.ToArray());

                    data.Clear();
                    totalLength = 0;

                    if(retries > 0)
                    {
                        Logger.LogInformation($"[{PlayerName}] Retried {retries} times and took {RetryWatch.ElapsedMilliseconds} milliseconds");
                        retries = 0;
                    }

                    if (ClientMessage.Item1 == MessageType.error)
                    {
                        throw new Exception($"[{PlayerName}] Error receiving message. Disconnecting player...");
                    }
                    else if(ClientMessage.Item1 == MessageType.ping)
                    {
                        var PingResult = new PingDTO();

                        if(ServerData.Configuration.PASSWORD != (string)ClientMessage.Item2)
                            PingResult = new PingDTO()
                            {
                                CorrectPassword = false,
                                Description = "",
                                PlayerList = new NamesDTO(),
                                GameMode = "",
                                PlayerLimit = 32
                            };
                        else
                            PingResult = new PingDTO()
                            {
                                CorrectPassword = true,
                                Description = ServerData.Configuration.DESCRIPTION,
                                PlayerList = ServerData.GetPlayers(),
                                GameMode = this.Gamemode,
                                PlayerLimit = 32
                            };

                        connection.Send(Encoding.UTF8.GetBytes(JsonConvert.SerializeObject(PingResult)));

                        connection.Close();
                        ClientConnected = false;

                    }
                    else if(ClientMessage.Item1 == MessageType.connect)
                    {
                        ConnectDTO UserConfiguration = (ConnectDTO)ClientMessage.Item2;
                        ConnectResponseDTO AssignationResult = ServerData.TryAssigning(UserConfiguration);

                        if(AssignationResult.Response != 1)
                        {
                            connection.Send(Encoding.UTF8.GetBytes(JsonConvert.SerializeObject(AssignationResult)));
                            connection.Close();
                            ClientConnected = false;
                            Logger.LogInformation($"Player {UserConfiguration.Name} tried to connect but failed with error {AssignationResult.Response}");
                            break;
                        }

                        PlayerNumber = AssignationResult.PlayerNumber;
                        PlayerName = ServerData.PlayerList[PlayerNumber].Name;
                        connection.Send(Encoding.UTF8.GetBytes(JsonConvert.SerializeObject(AssignationResult)));

                        Logger.LogInformation($"Player {UserConfiguration.Name} joined the server. Assigned to player {AssignationResult.PlayerNumber + 1}.");
                    }
                    else if(ClientMessage.Item1 == MessageType.update)
                    {
                        ServerData.SetConnection(PlayerNumber, true);

                        ClientDTO UserInformation = (ClientDTO)ClientMessage.Item2;

                        ServerData.UpdateWorldData(UserInformation.WorldData, PlayerNumber);
                        ServerData.UpdatePlayerData(UserInformation.PlayerData, PlayerNumber);
                        ServerData.UpdateEnemyData(UserInformation.EnemyData);
                        ServerData.UpdateQuestData(UserInformation.QuestData);

                        ServerDTO serverDTO = ServerData.GetData(PlayerNumber);
                        serverDTO.NetworkData.Map(this);

                        connection.Send(new JSONBuilder.JSONBuilder().BuildArrayOfBytes(serverDTO));

                        ServerData.ClearDeathSwap(PlayerNumber);
                    }
                    else if(ClientMessage.Item1 == MessageType.disconnect)
                    {
                        Logger.LogInformation($"Player {ServerData.GetPlayer(PlayerNumber).Name} disconnected. {(string)ClientMessage.Item2}");
                        connection.Close();
                        ClientConnected = false;
                        ServerData.SetConnection(PlayerNumber, false);
                    }
                }
                catch (Exception ex)
                {
                    Logger.LogInformation($"Player {ServerData.GetPlayer(PlayerNumber).Name} disconnected.", ex.Message);
                    Logger.LogDebug(ex.StackTrace);
                    ServerData.SetConnection(PlayerNumber, false);
                    connection.Close();
                    ClientConnected = false;
                }
            }
        }
    }
}
