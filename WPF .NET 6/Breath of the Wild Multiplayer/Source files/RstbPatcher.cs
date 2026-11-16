using CsYaz0;
using System;
using System.IO;
using System.Text;

namespace Breath_of_the_Wild_Multiplayer.Source_files
{
    /// <summary>
    /// Minimal editor for BotW's ResourceSizeTable (Wii U, big endian). The game allocates each resource's buffer
    /// from this table before loading it; a file larger than its entry overflows the heap and crashes the game
    /// while it boots (seen 17 Nov 2026: the merged TitleBG.pack was 12 KB over its entry).
    ///
    /// Format (after Yaz0): "RSTB", u32 crcCount, u32 nameCount, crcCount x (u32 crc32(name), u32 size),
    /// nameCount x (char[128] name, u32 size). Files without the "RSTB" magic only have the CRC table.
    /// </summary>
    public static class RstbPatcher
    {
        /// <summary>
        /// Makes sure every given resource's entry is at least its size plus headroom. Returns how many entries
        /// were raised. Entries are never lowered.
        /// </summary>
        public static int EnsureAtLeast(string rstbPath, params (string Name, uint MinSize)[] entries)
        {
            byte[] data = Yaz0.Decompress(File.ReadAllBytes(rstbPath));
            int changed = 0;

            bool hasHeader = data.Length >= 12 && Encoding.ASCII.GetString(data, 0, 4) == "RSTB";
            uint crcCount, nameCount;
            int crcStart;
            if (hasHeader)
            {
                crcCount = ReadU32(data, 4);
                nameCount = ReadU32(data, 8);
                crcStart = 12;
            }
            else
            {
                crcCount = (uint)(data.Length / 8);
                nameCount = 0;
                crcStart = 0;
            }
            int nameStart = crcStart + (int)crcCount * 8;

            foreach (var (name, minSize) in entries)
            {
                // Names that collide are stored in the name table instead of the CRC table.
                bool found = false;
                for (int i = 0; i < nameCount && !found; i++)
                {
                    int off = nameStart + i * 132;
                    string entryName = Encoding.ASCII.GetString(data, off, 128).TrimEnd('\0');
                    if (entryName == name)
                    {
                        found = true;
                        if (ReadU32(data, off + 128) < minSize) { WriteU32(data, off + 128, minSize); changed++; }
                    }
                }
                if (found) continue;

                uint crc = Crc32(Encoding.ASCII.GetBytes(name));
                for (int i = 0; i < crcCount; i++)
                {
                    int off = crcStart + i * 8;
                    if (ReadU32(data, off) == crc)
                    {
                        if (ReadU32(data, off + 4) < minSize) { WriteU32(data, off + 4, minSize); changed++; }
                        break;
                    }
                }
                // Not in the table: the game then sizes the buffer itself, nothing to do.
            }

            if (changed > 0)
                File.WriteAllBytes(rstbPath, Yaz0.Compress(data).ToArray());
            return changed;
        }

        private static uint ReadU32(byte[] d, int o) => (uint)(d[o] << 24 | d[o + 1] << 16 | d[o + 2] << 8 | d[o + 3]);

        private static void WriteU32(byte[] d, int o, uint v)
        {
            d[o] = (byte)(v >> 24); d[o + 1] = (byte)(v >> 16); d[o + 2] = (byte)(v >> 8); d[o + 3] = (byte)v;
        }

        private static readonly uint[] CrcTable = BuildCrcTable();

        private static uint[] BuildCrcTable()
        {
            var t = new uint[256];
            for (uint n = 0; n < 256; n++)
            {
                uint c = n;
                for (int k = 0; k < 8; k++) c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                t[n] = c;
            }
            return t;
        }

        private static uint Crc32(byte[] bytes)
        {
            uint c = 0xFFFFFFFFu;
            foreach (byte b in bytes) c = CrcTable[(c ^ b) & 0xFF] ^ (c >> 8);
            return c ^ 0xFFFFFFFFu;
        }
    }
}
