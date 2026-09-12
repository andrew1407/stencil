using System.Buffers.Binary;
using Stencil.TelegramBot.Application.Llm;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// Header parsing for the contract's accepted attachment formats (§7) against tiny
/// handcrafted byte arrays: PNG IHDR, GIF logical screen, JPEG SOFn, and the three WebP
/// flavours — plus refusal on truncated or foreign bytes.
/// </summary>
public sealed class ImageDimensionReaderTests
{
    internal static byte[] Png(int width, int height)
    {
        byte[] d = new byte[24];
        new byte[] { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A }.CopyTo(d, 0);
        d[12] = (byte)'I'; d[13] = (byte)'H'; d[14] = (byte)'D'; d[15] = (byte)'R';
        BinaryPrimitives.WriteInt32BigEndian(d.AsSpan(16), width);
        BinaryPrimitives.WriteInt32BigEndian(d.AsSpan(20), height);
        return d;
    }

    internal static byte[] Gif(int width, int height)
    {
        byte[] d = new byte[10];
        "GIF89a"u8.ToArray().CopyTo(d, 0);
        BinaryPrimitives.WriteUInt16LittleEndian(d.AsSpan(6), (ushort)width);
        BinaryPrimitives.WriteUInt16LittleEndian(d.AsSpan(8), (ushort)height);
        return d;
    }

    internal static byte[] Jpeg(int width, int height)
    {
        List<byte> d = [0xFF, 0xD8];                       // SOI
        d.AddRange([0xFF, 0xE0, 0x00, 0x04, 0x00, 0x00]);  // APP0, length 4 (skipped)
        d.AddRange([0xFF, 0xC0, 0x00, 0x11, 0x08,          // SOF0, length, precision
            (byte)(height >> 8), (byte)height, (byte)(width >> 8), (byte)width, 0x03]);
        return [.. d];
    }

    internal static byte[] WebpLossy(int width, int height)
    {
        byte[] d = new byte[30];
        "RIFF"u8.ToArray().CopyTo(d, 0);
        "WEBP"u8.ToArray().CopyTo(d, 8);
        "VP8 "u8.ToArray().CopyTo(d, 12);
        d[23] = 0x9D; d[24] = 0x01; d[25] = 0x2A;          // frame start code
        BinaryPrimitives.WriteUInt16LittleEndian(d.AsSpan(26), (ushort)width);
        BinaryPrimitives.WriteUInt16LittleEndian(d.AsSpan(28), (ushort)height);
        return d;
    }

    private static byte[] webpLossless(int width, int height)
    {
        byte[] d = new byte[30];
        "RIFF"u8.ToArray().CopyTo(d, 0);
        "WEBP"u8.ToArray().CopyTo(d, 8);
        "VP8L"u8.ToArray().CopyTo(d, 12);
        d[20] = 0x2F;                                      // lossless signature
        uint bits = (uint)(width - 1) | ((uint)(height - 1) << 14);
        BinaryPrimitives.WriteUInt32LittleEndian(d.AsSpan(21), bits);
        return d;
    }

    private static byte[] webpExtended(int width, int height)
    {
        byte[] d = new byte[30];
        "RIFF"u8.ToArray().CopyTo(d, 0);
        "WEBP"u8.ToArray().CopyTo(d, 8);
        "VP8X"u8.ToArray().CopyTo(d, 12);
        int w = width - 1, h = height - 1;                 // 24-bit little-endian canvas − 1
        d[24] = (byte)w; d[25] = (byte)(w >> 8); d[26] = (byte)(w >> 16);
        d[27] = (byte)h; d[28] = (byte)(h >> 8); d[29] = (byte)(h >> 16);
        return d;
    }

    [Fact]
    public void Should_Read_Every_Accepted_Formats_Header()
    {
        foreach ((byte[] bytes, int w, int h) in new (byte[], int, int)[]
        {
            (Png(640, 480), 640, 480),
            (Gif(320, 200), 320, 200),
            (Jpeg(2000, 1500), 2000, 1500),
            (WebpLossy(1024, 768), 1024, 768),
            (webpLossless(555, 44), 555, 44),
            (webpExtended(4000, 3000), 4000, 3000),
        })
        {
            Assert.True(ImageDimensionReader.TryRead(bytes, out int width, out int height));
            Assert.Equal(w, width);
            Assert.Equal(h, height);
        }
    }

    [Fact]
    public void Should_Refuse_Truncated_Or_Foreign_Bytes()
    {
        Assert.False(ImageDimensionReader.TryRead([], out _, out _));
        Assert.False(ImageDimensionReader.TryRead(Png(640, 480).AsSpan(0, 20), out _, out _));
        Assert.False(ImageDimensionReader.TryRead("not an image at all, just text bytes"u8, out _, out _));
        // A JPEG that ends before any SOF marker.
        Assert.False(ImageDimensionReader.TryRead([0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x04, 0x00, 0x00], out _, out _));
        // A RIFF that is not WebP.
        byte[] avi = new byte[30];
        "RIFF"u8.ToArray().CopyTo(avi, 0);
        "AVI "u8.ToArray().CopyTo(avi, 8);
        Assert.False(ImageDimensionReader.TryRead(avi, out _, out _));
    }

    [Fact]
    public void Should_Refuse_Zero_Dimensions()
    {
        Assert.False(ImageDimensionReader.TryRead(Png(0, 480), out _, out _));
        Assert.False(ImageDimensionReader.TryRead(Gif(320, 0), out _, out _));
    }
}
