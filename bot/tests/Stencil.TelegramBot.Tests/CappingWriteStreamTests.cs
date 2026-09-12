using Stencil.TelegramBot.Bot.Telegram;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The download cap: writes up to the limit pass through to the inner stream, and the first write
/// that would exceed it throws before any of the offending bytes land.
/// </summary>
public sealed class CappingWriteStreamTests
{
    [Fact]
    public async Task Should_Pass_Through_Writes_Up_To_The_Limit()
    {
        using MemoryStream sink = new();
        await using CappingWriteStream capped = new(sink, limit: 8);

        await capped.WriteAsync(new byte[5]);
        await capped.WriteAsync(new byte[3]); // exactly at the limit

        Assert.Equal(8, sink.Length);
    }

    [Fact]
    public async Task Should_Throw_When_Exceeding_The_Limit()
    {
        using MemoryStream sink = new();
        await using CappingWriteStream capped = new(sink, limit: 8);

        await capped.WriteAsync(new byte[8]);
        await Assert.ThrowsAsync<DownloadTooLargeException>(async () => await capped.WriteAsync(new byte[1]));
    }

    [Fact]
    public async Task Should_Not_Write_The_Overflowing_Chunk()
    {
        using MemoryStream sink = new();
        await using CappingWriteStream capped = new(sink, limit: 4);

        await Assert.ThrowsAsync<DownloadTooLargeException>(async () => await capped.WriteAsync(new byte[10]));
        Assert.Equal(0, sink.Length); // the whole over-limit chunk was rejected, not partially written
    }

    [Fact]
    public void Should_Leave_The_Inner_Stream_Open_After_Dispose()
    {
        MemoryStream sink = new();
        using (CappingWriteStream capped = new(sink, limit: 4))
        {
            capped.WriteByte(1);
        }
        // The wrapper must not close the caller-owned inner stream.
        sink.WriteByte(2);
        Assert.Equal(2, sink.Length);
    }
}
