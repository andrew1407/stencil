using System.Buffers.Binary;
using Stencil.TelegramBot.Domain.Editing;

namespace Stencil.TelegramBot.Application.Llm;

// Header sniffing for the §7 formats, so a small attachment skips the ffmpeg downscale and adopting
// an image needs no CLI probe.
public static class ImageDimensionReader
{
    private static readonly byte[] _pngSignature = [0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A];

    // A JPEG's frame header sits past any EXIF/ICC segments, so sniff a generous prefix.
    private const int _prefixBytes = 64 * 1024;

    public static async Task<ImageSize?> TryReadFileAsync(string path, CancellationToken ct = default)
    {
        if (!File.Exists(path))
        {
            return null;
        }
        await using FileStream stream = File.OpenRead(path);
        byte[] head = new byte[(int)Math.Min(stream.Length, _prefixBytes)];
        int read = await stream.ReadAtLeastAsync(head, head.Length, throwOnEndOfStream: false, ct);
        return TryRead(head.AsSpan(0, read), out int width, out int height) ? new ImageSize(width, height) : null;
    }

    public static bool TryRead(ReadOnlySpan<byte> data, out int width, out int height) =>
        tryReadPng(data, out width, out height)
        || tryReadGif(data, out width, out height)
        || tryReadJpeg(data, out width, out height)
        || tryReadWebp(data, out width, out height);

    private static bool tryReadPng(ReadOnlySpan<byte> d, out int width, out int height)
    {
        width = height = 0;
        if (d.Length < 24 || !d[..8].SequenceEqual(_pngSignature)
            || d[12] != 'I' || d[13] != 'H' || d[14] != 'D' || d[15] != 'R')
        {
            return false;
        }
        width = BinaryPrimitives.ReadInt32BigEndian(d[16..]);
        height = BinaryPrimitives.ReadInt32BigEndian(d[20..]);
        return width > 0 && height > 0;
    }

    private static bool tryReadGif(ReadOnlySpan<byte> d, out int width, out int height)
    {
        width = height = 0;
        if (d.Length < 10 || d[0] != 'G' || d[1] != 'I' || d[2] != 'F'
            || d[3] != '8' || (d[4] != '7' && d[4] != '9') || d[5] != 'a')
        {
            return false;
        }
        width = BinaryPrimitives.ReadUInt16LittleEndian(d[6..]);
        height = BinaryPrimitives.ReadUInt16LittleEndian(d[8..]);
        return width > 0 && height > 0;
    }

    private static bool tryReadJpeg(ReadOnlySpan<byte> d, out int width, out int height)
    {
        width = height = 0;
        if (d.Length < 4 || d[0] != 0xFF || d[1] != 0xD8)
        {
            return false;
        }
        int i = 2;
        while (i + 3 < d.Length)
        {
            if (d[i] != 0xFF)
            {
                return false; // lost sync — not a marker where one is expected
            }
            byte marker = d[i + 1];
            if (marker == 0xFF)
            {
                i++; // fill byte
                continue;
            }
            if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD9))
            {
                i += 2; // standalone marker (TEM/RSTn/SOI/EOI) — no length field
                continue;
            }
            int length = (d[i + 2] << 8) | d[i + 3];
            if (length < 2)
            {
                return false;
            }
            // SOF0–SOF15 carry the frame size; C4/C8/CC are DHT/JPG/DAC, not frames.
            bool isFrame = marker >= 0xC0 && marker <= 0xCF
                && marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
            if (isFrame)
            {
                if (i + 8 >= d.Length)
                {
                    return false;
                }
                height = (d[i + 5] << 8) | d[i + 6];
                width = (d[i + 7] << 8) | d[i + 8];
                return width > 0 && height > 0;
            }
            i += 2 + length;
        }
        return false;
    }

    private static bool tryReadWebp(ReadOnlySpan<byte> d, out int width, out int height)
    {
        width = height = 0;
        if (d.Length < 30
            || d[0] != 'R' || d[1] != 'I' || d[2] != 'F' || d[3] != 'F'
            || d[8] != 'W' || d[9] != 'E' || d[10] != 'B' || d[11] != 'P')
        {
            return false;
        }
        if (d[12] == 'V' && d[13] == 'P' && d[14] == '8' && d[15] == ' ')
        {
            // Lossy: a 3-byte frame tag, the 9D 01 2A start code, then 14-bit dimensions.
            if (d[23] != 0x9D || d[24] != 0x01 || d[25] != 0x2A)
            {
                return false;
            }
            width = BinaryPrimitives.ReadUInt16LittleEndian(d[26..]) & 0x3FFF;
            height = BinaryPrimitives.ReadUInt16LittleEndian(d[28..]) & 0x3FFF;
        }
        else if (d[12] == 'V' && d[13] == 'P' && d[14] == '8' && d[15] == 'L')
        {
            // Lossless: signature byte 0x2F, then two 14-bit (size − 1) fields.
            if (d[20] != 0x2F)
            {
                return false;
            }
            uint bits = BinaryPrimitives.ReadUInt32LittleEndian(d[21..]);
            width = (int)(bits & 0x3FFF) + 1;
            height = (int)((bits >> 14) & 0x3FFF) + 1;
        }
        else if (d[12] == 'V' && d[13] == 'P' && d[14] == '8' && d[15] == 'X')
        {
            // Extended: 24-bit little-endian (canvas size − 1) fields after 4 flag/reserved bytes.
            width = (d[24] | (d[25] << 8) | (d[26] << 16)) + 1;
            height = (d[27] | (d[28] << 8) | (d[29] << 16)) + 1;
        }
        else
        {
            return false;
        }
        return width > 0 && height > 0;
    }
}
