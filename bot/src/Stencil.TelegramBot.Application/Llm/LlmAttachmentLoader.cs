using System.Collections.Concurrent;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Application.Llm;

/// <summary>
/// Turns the working image into an LLM vision attachment per <c>llm-contract.md</c> §7:
/// only the contract's accepted media types are attached, and anything larger than
/// <see cref="LlmImage.MaxLongEdgePixels"/> on the long edge is downscaled (shrink-only,
/// re-encoded as PNG) through <see cref="IImageDownscaler"/> before base64-encoding. When the
/// header proves the image is already small enough, the original bytes ride along untouched;
/// when the downscale can't run (no ffmpeg, failure), it degrades to the original bytes rather
/// than breaking <c>/prompt</c> — but only up to <see cref="MaxOriginalBytes"/>, past which the
/// turn goes text-only instead of uploading megabytes the scaler was meant to shrink.
/// </summary>
/// <remarks>
/// Built attachments are memoised per path keyed on (mtime, length) — chat mode re-attaches
/// the working image on every turn, and re-reading + re-scaling + re-encoding an unchanged
/// file is pure waste (the same reason <c>pystencil</c> keeps its <c>_png_cache</c>). This
/// type is a DI singleton, so the cache is shared and kept small.
/// </remarks>
public sealed class LlmAttachmentLoader
{
    /// <summary>Extension → media type for the contract's accepted attachment formats (§7).</summary>
    private static readonly Dictionary<string, string> MediaTypes = new(StringComparer.OrdinalIgnoreCase)
    {
        [".png"] = "image/png",
        [".jpg"] = "image/jpeg",
        [".jpeg"] = "image/jpeg",
        [".webp"] = "image/webp",
        [".gif"] = "image/gif",
    };

    /// <summary>
    /// Longest header prefix the dimension sniff reads — enough for every §7 format (a JPEG
    /// SOF can trail sizeable EXIF/ICC segments) without slurping a whole oversized file the
    /// ffmpeg branch would then re-read anyway.
    /// </summary>
    private const int SniffPrefixBytes = 64 * 1024;

    /// <summary>Cache bound; base64 payloads are big, so the cache is simply cleared when full.</summary>
    private const int MaxCachedImages = 16;

    /// <summary>Ceiling on an attachment sent WITHOUT downscaling, matching cli/mcp's threshold.</summary>
    private const long MaxOriginalBytes = 8L * 1024 * 1024;

    private sealed record CachedImage(long LastWriteTicks, long Length, LlmImage Image);

    private readonly IImageDownscaler _downscaler;
    private readonly ConcurrentDictionary<string, CachedImage> _cache = new();

    public LlmAttachmentLoader(IImageDownscaler downscaler)
    {
        _downscaler = downscaler;
    }

    /// <summary>
    /// Load one image file as an attachment, or null when there is nothing attachable (no
    /// path, missing file, or a format outside the accepted set — the turn is then text-only).
    /// </summary>
    public async Task<LlmImage?> LoadAsync(string? path, CancellationToken ct = default)
    {
        if (path is null || !MediaTypes.TryGetValue(Path.GetExtension(path), out string? mediaType))
        {
            return null;
        }
        FileInfo file = new(path);
        if (!file.Exists)
        {
            return null;
        }
        long stamp = file.LastWriteTimeUtc.Ticks;
        long length = file.Length;
        if (_cache.TryGetValue(path, out CachedImage? cached)
            && cached.LastWriteTicks == stamp && cached.Length == length)
        {
            return cached.Image;
        }
        LlmImage? image = await BuildAsync(path, mediaType, length, ct);
        if (image is null)
        {
            return null;   // too large to send as-is; not cached, so a retry re-tries the scaler
        }
        if (_cache.Count >= MaxCachedImages)
        {
            _cache.Clear();
        }
        _cache[path] = new CachedImage(stamp, length, image);
        return image;
    }

    /// <summary>
    /// Build the attachment: sniff the header, downscale when oversized, base64-encode. Null when
    /// the image is too large to send and could not be scaled down.
    /// </summary>
    private async Task<LlmImage?> BuildAsync(string path, string mediaType, long length, CancellationToken ct)
    {
        byte[] prefix = await ReadPrefixAsync(path, (int)Math.Min(length, SniffPrefixBytes), ct);
        if (ImageDimensionReader.TryRead(prefix, out int width, out int height)
            && Math.Max(width, height) <= LlmImage.MaxLongEdgePixels)
        {
            byte[] bytes = prefix.LongLength == length
                ? prefix
                : await File.ReadAllBytesAsync(path, ct);
            return new LlmImage(mediaType, Convert.ToBase64String(bytes));
        }
        // Oversized — or a header we couldn't read, where a shrink-only pass is a no-op risk
        // worth taking.
        byte[]? scaled = await _downscaler.DownscaleToPngAsync(path, LlmImage.MaxLongEdgePixels, ct);
        if (scaled is not null)
        {
            return new LlmImage("image/png", Convert.ToBase64String(scaled));
        }
        // Downscaling failed (no ffmpeg, bad format, timeout). Sending the original is fine
        // for an ordinary photo, but its only other bound is the 50 MB download cap, which
        // base64 inflates by a third — past MaxOriginalBytes a text-only turn is better.
        if (length > MaxOriginalBytes)
        {
            return null;
        }
        return new LlmImage(mediaType, Convert.ToBase64String(await File.ReadAllBytesAsync(path, ct)));
    }

    /// <summary>Read at most <paramref name="count"/> leading bytes of a file.</summary>
    private static async Task<byte[]> ReadPrefixAsync(string path, int count, CancellationToken ct)
    {
        await using FileStream stream = File.OpenRead(path);
        byte[] buffer = new byte[count];
        int read = await stream.ReadAtLeastAsync(buffer, count, throwOnEndOfStream: false, ct);
        return read == buffer.Length ? buffer : buffer[..read];
    }
}
