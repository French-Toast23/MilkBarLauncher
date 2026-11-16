using System;
using System.Configuration;
using System.Threading;

namespace Breath_of_the_Wild_Multiplayer
{
    /// <summary>
    /// Saves the user settings without crashing. Two launchers started at the same time (one per Cemu
    /// instance) write the same user.config, and the loser of that race used to crash on startup
    /// (ConfigurationErrorsException "Unable to save config to file", 16 Nov 2026).
    /// </summary>
    public static class SettingsSaver
    {
        private static readonly object Gate = new object();

        public static void SaveSafe()
        {
            lock (Gate)
            {
                for (int attempt = 0; attempt < 5; attempt++)
                {
                    try
                    {
                        Properties.Settings.Default.Save();
                        return;
                    }
                    catch (ConfigurationErrorsException)
                    {
                        Thread.Sleep(100 * (attempt + 1));
                    }
                    catch (Exception)
                    {
                        return; // settings are a convenience; never take the launcher down for them
                    }
                }
            }
        }
    }
}
