using System.Collections.Concurrent;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Application.Llm;

// §7 attachments: accepted media types only; anything over LlmImage.MAX_LONG_EDGE_PIXELS is downscaled
// (shrink-only PNG) via IImageDownscaler, and a failed downscale degrades to the original up to
// MaxOriginalBytes. Attachments are memoised per path on (mtime, length): chat mode re-attaches the
// same image every turn.
public sealed class LlmAttachmentLoader
{
    private static readonly Dictionary<string, string> _mediaTypes = new(StringComparer.OrdinalIgnoreCase)
    {
        [".png"] = "image/png",
        [".jpg"] = "image/jpeg",
        [".jpeg"] = "image/jpeg",
        [".webp"] = "image/webp",
        [".gif"] = "image/gif",
    };

    // A JPEG SOF can trail sizeable EXIF/ICC segments; 64 KB covers every §7 format.
    private const int _sniffPrefixBytes = 64 * 1024;

    // Base64 payloads are big, so the cache is simply cleared when full.
    private const int _maxCachedImages = 16;

    /// <summary>Ceiling on an attachment sent WITHOUT downscaling, matching cli/mcp's threshold.</summary>
    private const long _maxOriginalBytes = 8L * 1024 * 1024;

    private sealed record CachedImage(long LastWriteTicks, long Length, LlmImage Image);

    private readonly IImageDownscaler _downscaler;
    private readonly ConcurrentDictionary<string, CachedImage> _cache = new();

    public LlmAttachmentLoader(IImageDownscaler downscaler)
    {
        _downscaler = downscaler;
    }

    public async Task<LlmImage?> LoadAsync(string? path, CancellationToken ct = default)
    {
        if (path is null || !_mediaTypes.TryGetValue(Path.GetExtension(path), out string? mediaType))
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
        LlmImage? image = await buildAsync(path, mediaType, length, ct);
        if (image is null)
        {
            return null;   // too large to send as-is; not cached, so a retry re-tries the scaler
        }
        if (_cache.Count >= _maxCachedImages)
        {
            _cache.Clear();
        }
        _cache[path] = new CachedImage(stamp, length, image);
        return image;
    }

    private async Task<LlmImage?> buildAsync(string path, string mediaType, long length, CancellationToken ct)
    {
        byte[] prefix = await readPrefixAsync(path, (int)Math.Min(length, _sniffPrefixBytes), ct);
        if (ImageDimensionReader.TryRead(prefix, out int width, out int height)
            && Math.Max(width, height) <= LlmImage.MAX_LONG_EDGE_PIXELS)
        {
            byte[] bytes = prefix.LongLength == length
                ? prefix
                : await File.ReadAllBytesAsync(path, ct);
            return new LlmImage(mediaType, Convert.ToBase64String(bytes));
        }
        // Oversized, or a header we couldn't read (a shrink-only pass is a no-op at worst).
        byte[]? scaled = await _downscaler.DownscaleToPngAsync(path, LlmImage.MAX_LONG_EDGE_PIXELS, ct);
        if (scaled is not null)
        {
            return new LlmImage("image/png", Convert.ToBase64String(scaled));
        }
        // Past MaxOriginalBytes (base64 inflates by a third) a text-only turn beats uploading
        // megabytes.
        if (length > _maxOriginalBytes)
        {
            return null;
        }
        return new LlmImage(mediaType, Convert.ToBase64String(await File.ReadAllBytesAsync(path, ct)));
    }

    private static async Task<byte[]> readPrefixAsync(string path, int count, CancellationToken ct)
    {
        await using FileStream stream = File.OpenRead(path);
        byte[] buffer = new byte[count];
        int read = await stream.ReadAtLeastAsync(buffer, count, throwOnEndOfStream: false, ct);
        return read == buffer.Length ? buffer : buffer[..read];
    }
}
