using Stencil.TelegramBot.Domain.Abstractions;

namespace Stencil.TelegramBot.Tests.Doubles;

/// <summary>
/// An in-process <see cref="IImageDownscaler"/> stand-in: it records every call and returns a
/// configurable result. The default (null) mimics a machine without ffmpeg, so callers exercise
/// the fall-back-to-original-bytes path.
/// </summary>
public sealed class MockImageDownscaler : IImageDownscaler
{
    /// <summary>Every (path, maxLongEdge) this mock was asked to downscale, in order.</summary>
    public List<(string Path, int MaxLongEdge)> Calls { get; } = new();

    /// <summary>The bytes to return; null (the default) simulates ffmpeg being unavailable.</summary>
    public byte[]? Result { get; set; }

    public Task<byte[]?> DownscaleToPngAsync(string path, int maxLongEdge, CancellationToken ct = default)
    {
        Calls.Add((path, maxLongEdge));
        return Task.FromResult(Result);
    }
}
