namespace Stencil.TelegramBot.Bot.Telegram;

/// <summary>
/// A write-only pass-through stream that throws once more than a byte limit has been written,
/// used to cap how much the bot will download from Telegram into memory/disk. The inner stream is
/// never closed by this wrapper (its owner disposes it), so partial output can still be cleaned up.
/// </summary>
public sealed class CappingWriteStream : Stream
{
    private readonly Stream _inner;
    private readonly long _limit;
    private long _written;

    public CappingWriteStream(Stream inner, long limit)
    {
        _inner = inner;
        _limit = limit;
    }

    /// <summary>Account for a pending write, throwing before it lands if it would breach the cap.</summary>
    private void Account(long count)
    {
        _written += count;
        if (_written > _limit)
        {
            throw new DownloadTooLargeException(_limit);
        }
    }

    public override void Write(byte[] buffer, int offset, int count)
    {
        Account(count);
        _inner.Write(buffer, offset, count);
    }

    public override void Write(ReadOnlySpan<byte> buffer)
    {
        Account(buffer.Length);
        _inner.Write(buffer);
    }

    public override Task WriteAsync(byte[] buffer, int offset, int count, CancellationToken cancellationToken)
    {
        Account(count);
        return _inner.WriteAsync(buffer, offset, count, cancellationToken);
    }

    public override ValueTask WriteAsync(ReadOnlyMemory<byte> buffer, CancellationToken cancellationToken = default)
    {
        Account(buffer.Length);
        return _inner.WriteAsync(buffer, cancellationToken);
    }

    public override void WriteByte(byte value)
    {
        Account(1);
        _inner.WriteByte(value);
    }

    public override void Flush() => _inner.Flush();

    public override Task FlushAsync(CancellationToken cancellationToken) => _inner.FlushAsync(cancellationToken);

    public override bool CanRead => false;
    public override bool CanSeek => false;
    public override bool CanWrite => true;
    public override long Length => _inner.Length;

    public override long Position
    {
        get => _inner.Position;
        set => throw new NotSupportedException();
    }

    public override int Read(byte[] buffer, int offset, int count) => throw new NotSupportedException();

    public override long Seek(long offset, SeekOrigin origin) => throw new NotSupportedException();

    public override void SetLength(long value) => throw new NotSupportedException();
}

/// <summary>
/// Raised when a Telegram download exceeds the configured byte cap. Derives from
/// <see cref="InvalidOperationException"/> so the router's error guard surfaces its message verbatim.
/// </summary>
public sealed class DownloadTooLargeException : InvalidOperationException
{
    public DownloadTooLargeException(long limitBytes)
        : base($"That file is too large — the bot's limit is {limitBytes / (1024 * 1024)} MB.")
    {
    }
}
