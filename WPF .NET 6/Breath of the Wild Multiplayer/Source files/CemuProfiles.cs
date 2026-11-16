using Newtonsoft.Json;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace Breath_of_the_Wild_Multiplayer.Source_files
{
    /// <summary>
    /// Up to four Cemu installs the launcher can start. The list lives in %APPDATA%/BOTWM/cemu_profiles.json,
    /// so every launcher window sees the same slots. Which slot a window uses (<see cref="Active"/>) is kept per
    /// launcher process: two launchers can drive two different Cemus at the same time. Everything that resolves
    /// the Cemu folder (paths, install / repair / uninstall, connect) follows the active slot.
    /// </summary>
    public static class CemuProfiles
    {
        public const int SlotCount = 4;

        private class Store
        {
            public string[] Slots = new string[SlotCount];
            public int Selected;
        }

        private static readonly object Gate = new object();
        private static Store _store;
        private static int _active = -1;

        /// <summary>Raised (on the caller's thread) when the active slot or a slot path changes.</summary>
        public static event Action Changed;

        private static string StorePath => Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "BOTWM", "cemu_profiles.json");

        private static Store Load()
        {
            lock (Gate)
            {
                if (_store != null)
                    return _store;

                try
                {
                    if (File.Exists(StorePath))
                        _store = JsonConvert.DeserializeObject<Store>(File.ReadAllText(StorePath));
                }
                catch { }

                if (_store == null)
                {
                    // First run: carry over the old single "Cemu location" setting (or what auto-detection finds).
                    _store = new Store();
                    try
                    {
                        string legacy = Properties.Settings.Default.cemuLocation;
                        if (!string.IsNullOrEmpty(legacy) && (File.Exists(legacy) || Directory.Exists(legacy)))
                            _store.Slots[0] = legacy;
                    }
                    catch { }
                    if (string.IsNullOrEmpty(_store.Slots[0]))
                    {
                        string detected = BcmlSettings.AutoDetectCemuDir();
                        if (!string.IsNullOrEmpty(detected))
                            _store.Slots[0] = detected;
                    }
                    SaveLocked();
                }

                if (_store.Slots == null || _store.Slots.Length != SlotCount)
                {
                    var slots = new string[SlotCount];
                    if (_store.Slots != null)
                        Array.Copy(_store.Slots, slots, Math.Min(SlotCount, _store.Slots.Length));
                    _store.Slots = slots;
                }
                if (_store.Selected < 0 || _store.Selected >= SlotCount)
                    _store.Selected = 0;
                return _store;
            }
        }

        private static void SaveLocked()
        {
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(StorePath)!);
                File.WriteAllText(StorePath, JsonConvert.SerializeObject(_store, Formatting.Indented));
            }
            catch { } // another launcher may be writing it right now; the in-memory copy stays correct
        }

        /// <summary>Raw slot value (a Cemu.exe path or a Cemu folder), or null.</summary>
        public static string GetSlot(int index)
        {
            if (index < 0 || index >= SlotCount) return null;
            return Load().Slots[index];
        }

        public static void SetSlot(int index, string path)
        {
            if (index < 0 || index >= SlotCount) return;
            lock (Gate)
            {
                Load().Slots[index] = string.IsNullOrWhiteSpace(path) ? null : path;
                SaveLocked();
            }
            if (index == 0)
            {
                // keep the old setting in sync for anything that still reads it
                try { Properties.Settings.Default.cemuLocation = path ?? ""; SettingsSaver.SaveSafe(); } catch { }
            }
            Changed?.Invoke();
        }

        /// <summary>Folder of a slot's Cemu.exe, or null when the slot is empty or no longer exists.</summary>
        public static string GetSlotDir(int index)
        {
            string value = GetSlot(index);
            if (string.IsNullOrEmpty(value)) return null;
            try
            {
                if (File.Exists(value))
                    return Path.GetDirectoryName(value);
                if (Directory.Exists(value))
                    return value;
            }
            catch { }
            return null;
        }

        public static bool IsConfigured(int index) => !string.IsNullOrEmpty(GetSlotDir(index));

        public static IEnumerable<int> ConfiguredSlots => Enumerable.Range(0, SlotCount).Where(IsConfigured);

        /// <summary>Slot this launcher window uses. Starts at the last slot chosen in any launcher.</summary>
        public static int Active
        {
            get
            {
                if (_active < 0)
                    _active = Load().Selected;
                return _active;
            }
            set
            {
                if (value < 0 || value >= SlotCount || value == Active) return;
                _active = value;
                lock (Gate)
                {
                    Load().Selected = value; // default for the next launcher that opens
                    SaveLocked();
                }
                Changed?.Invoke();
            }
        }

        /// <summary>Sets the slot for this launcher only, without changing the default other launchers start with.</summary>
        public static void SetActiveForThisProcess(int index)
        {
            if (index < 0 || index >= SlotCount || index == Active) return;
            _active = index;
            Changed?.Invoke();
        }

        /// <summary>True when a Cemu started from this slot's folder is running.</summary>
        public static bool IsSlotRunning(int index)
        {
            string dir = GetSlotDir(index);
            if (dir == null) return false;
            string full = Path.GetFullPath(dir).TrimEnd('\\');
            foreach (var p in System.Diagnostics.Process.GetProcessesByName("Cemu"))
            {
                try
                {
                    string exeDir = Path.GetDirectoryName(p.MainModule?.FileName ?? "");
                    if (!string.IsNullOrEmpty(exeDir) && string.Equals(Path.GetFullPath(exeDir).TrimEnd('\\'), full, StringComparison.OrdinalIgnoreCase))
                        return true;
                }
                catch { }
            }
            return false;
        }

        /// <summary>Selects the next (or previous) configured slot. Does nothing with fewer than two.</summary>
        public static void Cycle(int direction)
        {
            var configured = ConfiguredSlots.ToList();
            if (configured.Count == 0) return;
            int pos = configured.IndexOf(Active);
            pos = pos < 0 ? 0 : (pos + direction + configured.Count) % configured.Count;
            Active = configured[pos];
        }

        // ------------------------------------------------------------------ portable copies

        /// <summary>Cemu's shared data folder, used by every Cemu without a "portable" folder.</summary>
        public static string SharedUserDataDir => Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Cemu");

        public static bool IsPortable(string cemuDir) =>
            !string.IsNullOrEmpty(cemuDir) && Directory.Exists(Path.Combine(cemuDir, "portable"));

        // Setting up a portable copy (download, seeding, game links) lives in CemuSetup.
        /// <summary>"Cemu 2 - cemu_2.6_P2", or "Cemu 2 (not set)".</summary>
        public static string Describe(int index)
        {
            string dir = GetSlotDir(index);
            return dir == null
                ? $"Cemu {index + 1} (not set)"
                : $"Cemu {index + 1} - {new DirectoryInfo(dir).Name}";
        }
    }
}
