using System.Net;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Net;

namespace Stencil.TelegramBot.Infrastructure.Links;

// Guarded GET for /layout <url>, bounded by the Telegram download cap. Callers SSRF-vet the URL first
// (RemoteImageUrl.ValidateAsync); the resolved address is re-checked at connect time against rebinding.
public sealed class LayoutFetcher : IDisposable
{
    private readonly HttpClient _http;
    private readonly long _maxBytes;

    private const string _refusal = "That link resolves to a private or local address, which isn't allowed.";

    // A test handler bypasses the connect-time guard; in production isBlockedAddress is enforced on
    // the dialled IP.
    public LayoutFetcher(
        BotOptions options,
        HttpMessageHandler? handler = null,
        Func<IPAddress, bool>? isBlockedAddress = null)
    {
        _http = new HttpClient(handler ?? GuardedConnect.Handler(isBlockedAddress, _refusal));
        _http.Timeout = options.ServerHttpTimeout;
        _maxBytes = options.MaxDownloadBytes;
    }

    // Null on a non-success status (redirects included); throws past the download cap or on a guard
    // rejection.
    public async Task<byte[]?> FetchAsync(string url, CancellationToken ct = default)
    {
        HttpResponseMessage response;
        try
        {
            response = await _http.GetAsync(url, HttpCompletionOption.ResponseHeadersRead, ct);
        }
        catch (HttpRequestException ex) when (GuardedConnect.RefusalIn(ex) is InvalidOperationException blocked)
        {
            // The guard's verbatim message (SafeAsync shows it) rather than the transport error
            // wrapping it.
            throw blocked;
        }
        // HttpClient.Timeout surfaces as a cancellation the caller never asked for.
        catch (TaskCanceledException) when (!ct.IsCancellationRequested)
        {
            throw new InvalidOperationException(
                $"The layout link didn't answer within {_http.Timeout.TotalSeconds:0} s.");
        }
        using (response)
        {
            if (!response.IsSuccessStatusCode)
            {
                return null;
            }
            return await CappedBody.ReadAsync(response.Content, _maxBytes, ct)
                ?? throw new InvalidOperationException(
                    $"Layout download exceeds the {_maxBytes / (1024 * 1024)} MB limit.");
        }
    }

    public void Dispose() => _http.Dispose();
}
