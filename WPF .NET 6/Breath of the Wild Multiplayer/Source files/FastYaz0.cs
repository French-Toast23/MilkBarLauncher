using System;
using System.IO;

namespace Breath_of_the_Wild_Multiplayer.Source_files
{
    public static class FastYaz0
    {
        public static byte[] Compress(ReadOnlySpan<byte> src, int maxChain = 16)
        {
            int srcLen = src.Length;
            byte[] dst = new byte[srcLen + srcLen / 8 + 32];

            // Yaz0 16-byte Header
            dst[0] = (byte)'Y';
            dst[1] = (byte)'a';
            dst[2] = (byte)'z';
            dst[3] = (byte)'0';
            dst[4] = (byte)((srcLen >> 24) & 0xFF);
            dst[5] = (byte)((srcLen >> 16) & 0xFF);
            dst[6] = (byte)((srcLen >> 8) & 0xFF);
            dst[7] = (byte)(srcLen & 0xFF);

            int dstPos = 16;
            int srcPos = 0;

            int[] head = new int[65536];
            Array.Fill(head, -1);
            int[] prev = new int[srcLen];

            while (srcPos < srcLen)
            {
                int codeBytePos = dstPos++;
                byte codeByte = 0;

                for (int bit = 7; bit >= 0; bit--)
                {
                    if (srcPos >= srcLen)
                        break;

                    int bestMatchPos = 0;
                    int bestMatchLen = 0;

                    if (srcPos + 2 < srcLen)
                    {
                        int hash = ((src[srcPos] << 8) | src[srcPos + 1]) ^ (src[srcPos + 2] << 4);
                        hash &= 0xFFFF;

                        int chainLen = 0;
                        int cur = head[hash];

                        while (cur != -1 && (srcPos - cur) <= 4096 && chainLen++ < maxChain)
                        {
                            int maxLen = Math.Min(273, srcLen - srcPos);
                            int len = 0;
                            while (len < maxLen && src[cur + len] == src[srcPos + len])
                                len++;

                            if (len > bestMatchLen)
                            {
                                bestMatchLen = len;
                                bestMatchPos = cur;
                                if (len == 273) break;
                            }

                            cur = prev[cur];
                        }

                        prev[srcPos] = head[hash];
                        head[hash] = srcPos;
                    }

                    if (bestMatchLen >= 3)
                    {
                        int offset = srcPos - bestMatchPos;
                        if (bestMatchLen >= 18)
                        {
                            dst[dstPos++] = (byte)((offset - 1) >> 8);
                            dst[dstPos++] = (byte)((offset - 1) & 0xFF);
                            dst[dstPos++] = (byte)(bestMatchLen - 18);
                        }
                        else
                        {
                            dst[dstPos++] = (byte)((((bestMatchLen - 2) & 0xF) << 4) | (((offset - 1) >> 8) & 0xF));
                            dst[dstPos++] = (byte)((offset - 1) & 0xFF);
                        }

                        for (int k = 1; k < bestMatchLen; k++)
                        {
                            int p = srcPos + k;
                            if (p + 2 < srcLen)
                            {
                                int h = ((src[p] << 8) | src[p + 1]) ^ (src[p + 2] << 4);
                                h &= 0xFFFF;
                                prev[p] = head[h];
                                head[h] = p;
                            }
                        }

                        srcPos += bestMatchLen;
                    }
                    else
                    {
                        codeByte |= (byte)(1 << bit);
                        dst[dstPos++] = src[srcPos++];
                    }
                }

                dst[codeBytePos] = codeByte;
            }

            Array.Resize(ref dst, dstPos);
            return dst;
        }
    }
}
