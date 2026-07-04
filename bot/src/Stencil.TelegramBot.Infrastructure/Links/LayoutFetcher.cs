using Stencil.TelegramBot.Infrastructure.Configuration;

namespace Stencil.TelegramBot.Infrastructure.Links;

/// <summary>
/// Small guarded HTTP GET for <c>/layout &lt;url&gt;</c>: fetches a layout JSON body, bounded
/// by the same download cap as Telegram file downloads. Callers SSRF-vet the URL first
/// (<c>RemoteImageUrl.ValidateAsync</c>) — this class only moves the bytes.
/// </summary>
public sealed class LayoutFetcher : IDisposable
{
    private readonly HttpClient _http;
    private readonly long _maxBytes;

    /// <summary>`handler` lets tests inject a canned <see cref="HttpMessageHandler"/>.</summary>
    public LayoutFetcher(BotOptions options, HttpMessageHandler? handler = null)
    {
        // Redirects are refused: the URL was SSRF-vetted BEFORE this call, and following
        // a redirect would let a vetted public host bounce the request to a private /
        // link-local / cloud-metadata one the guard would have rejected.
        _http = new HttpClient(handler ?? new HttpClientHandler { AllowAutoRedirect = false });
        _http.Timeout = options.ServerHttpTimeout;
        _maxBytes = options.MaxDownloadBytes;
    }

    /// <summary>
    /// GET the body, or null on a non-success status (redirects included — see the ctor).
    /// Throws when the body exceeds the download cap.
    /// </summary>
    public async Task<byte[]?> FetchAsync(string url, CancellationToken ct = default)
    {
        using HttpResponseMessage response =
            await _http.GetAsync(url, HttpCompletionOption.ResponseHeadersRead, ct);
        if (!response.IsSuccessStatusCode)
        {
            return null;
        }
        await using Stream body = await response.Content.ReadAsStreamAsync(ct);
        using MemoryStream buffer = new();
        byte[] chunk = new byte[81920];
        int read;
        while ((read = await body.ReadAsync(chunk, ct)) > 0)
        {
            if (buffer.Length + read > _maxBytes)
            {
                throw new InvalidOperationException(
                    $"Layout download exceeds the {_maxBytes / (1024 * 1024)} MB limit.");
            }
            buffer.Write(chunk, 0, read);
        }
        return buffer.ToArray();
    }

    public void Dispose() => _http.Dispose();
}
