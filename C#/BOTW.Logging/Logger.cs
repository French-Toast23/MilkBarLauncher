namespace BOTW.Logging
{
    public static class Logger
    {
        public enum LogLevelEnum : int
        {
            INFO = 2,
            WARNING = 3,
            DEBUG = 4
        }

        public enum LogWriteLevelEnum : int
        {
            CRT = 0,
            ERR = 1,
            INF = 2,
            WRN = 3,
            DBG = 4
        }

        private static LogLevelEnum CMDLevel;
        private static LogLevelEnum LogLevel;

        private static string CurrentDirectory = Directory.GetCurrentDirectory();
        private static string LogPath = $"{CurrentDirectory}\\LatestLog.txt";
        private static string LogsFolder = $"{CurrentDirectory}\\Logs";

        private static readonly object logLock = new object();

        public static void Start(LogLevelEnum logLevel, LogLevelEnum cmdLevel)
        {
            LogLevel = logLevel;
            CMDLevel = cmdLevel;

            SetupLogFile();
        }

        public static void LogCritical(string message, string details = "", ConsoleColor color = ConsoleColor.DarkRed) => WriteToLog(LogWriteLevelEnum.CRT, message, details, color);
        public static void LogError(string message, string details = "", ConsoleColor color = ConsoleColor.DarkRed) => WriteToLog(LogWriteLevelEnum.ERR, message, details, color);
        public static void LogInformation(string message, string details = "", ConsoleColor color = ConsoleColor.White) => WriteToLog(LogWriteLevelEnum.INF, message, details, color);
        public static void LogWarning(string message, string details = "", ConsoleColor color = ConsoleColor.DarkYellow) => WriteToLog(LogWriteLevelEnum.WRN, message, details, color);
        public static void LogDebug(string message, string details = "", ConsoleColor color = ConsoleColor.Magenta) => WriteToLog(LogWriteLevelEnum.DBG, message, details, color);

        public static string LogInput(string message)
        {
            if(!string.IsNullOrEmpty(message))
                WriteToLog(LogWriteLevelEnum.INF, message, "", ConsoleColor.White, false);

            string userInput = Console.ReadLine();

            lock (logLock)
            {
                try
                {
                    using (FileStream fs = new FileStream(LogPath, FileMode.Append, FileAccess.Write, FileShare.ReadWrite))
                    using (StreamWriter LogFile = new StreamWriter(fs))
                    {
                        LogFile.WriteLine(userInput);
                    }
                }
                catch
                {
                }
            }

            return userInput;
        }

        private static void SetupLogFile()
        {
            try
            {
                if(File.Exists(LogPath))
                {
                    string creationTime = File.GetCreationTime(LogPath).ToString("yyyy-MM-dd, HH-mm-ss");

                    if (!Directory.Exists(LogsFolder))
                        Directory.CreateDirectory(LogsFolder);

                    string postName = "";

                    // The correct way to manage a log file already existing is by creating a differently named log file
                    if(File.Exists($"{LogsFolder}\\{creationTime}.txt"))
                        postName = $"({Directory.GetFiles(LogsFolder).Where(file => Path.GetFileNameWithoutExtension(file).StartsWith(creationTime)).Count()})";

                    if (!File.Exists($"{LogsFolder}\\{creationTime}{postName}.txt"))
                    {
                        File.Move(LogPath, $"{LogsFolder}\\{creationTime}{postName}.txt");
                    }
                    else
                    {
                        // If for some reason, even after naming the file differently, it fails. Here we can delete it.
                        Console.WriteLine($"Failed to create file at: {LogsFolder}\\{creationTime}{postName}.txt. Deleting it...");
                        File.Delete(LogPath);
                    }
                }

                File.WriteAllText(LogPath, string.Empty);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[Warning] Could not initialize log file: {ex.Message}");
            }
        }

        private static void WriteToLog(LogWriteLevelEnum writeLevel, string message, string details, ConsoleColor color, bool newLine = true, bool writeToCMD = true)
        {
            string MessageTime = DateTime.Now.ToString("HH:mm:ss");

            if(writeToCMD)
                WriteToConsole(writeLevel, message, MessageTime, color, newLine);

            if ((int)writeLevel > (int)LogLevel)
                return;

            lock (logLock)
            {
                try
                {
                    using (FileStream fs = new FileStream(LogPath, FileMode.Append, FileAccess.Write, FileShare.ReadWrite))
                    using (StreamWriter LogFile = new StreamWriter(fs))
                    {
                        LogFile.Write($"[{MessageTime}] {message}");

                        if (!string.IsNullOrEmpty(details))
                            LogFile.Write($" - Details: {details}");

                        if (newLine)
                            LogFile.WriteLine();
                    }
                }
                catch(Exception ex)
                {
                    WriteToConsole(LogWriteLevelEnum.ERR, $"\nCould not write to log file: {ex.Message}. Please make sure that you don't have another server process and restart your server.", MessageTime, ConsoleColor.DarkRed, true);
                }
            }
        }

        private static void WriteToConsole(LogWriteLevelEnum writeLevel, string message, string datetime, ConsoleColor color, bool newLine)
        {
            if ((int)writeLevel > (int)CMDLevel)
                return;

            Console.ForegroundColor = ConsoleColor.Gray;

            Console.Write($"[{datetime}] ");

            Console.ForegroundColor = color;

            if (newLine)
                Console.WriteLine(message);
            else
                Console.Write(message);


            Console.ForegroundColor = ConsoleColor.White;
        }
    }
}
