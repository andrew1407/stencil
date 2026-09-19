using System.Net;
using System.Net.Sockets;
using Stencil.TelegramBot.Infrastructure.Configuration;

namespace Stencil.TelegramBot.Infrastructure.Links;

// Guarded GET for /layout <url>, bounded by the Telegram download cap. Callers SSRF-vet the URL first
// (RemoteImageUrl.ValidateAsync); the resolved address is re-checked at connect time against rebinding.
public sealed class LayoutFetcher : IDisposable
{
    private readonly HttpClient _http;
    private readonly long _maxBytes;

    // A test handler bypasses the connect-time guard; in production isBlockedAddress is enforced on
    // the dialled IP.
    public LayoutFetcher(
        BotOptions options,
        HttpMessageHandler? handler = null,
        Func<IPAddress, bool>? isBlockedAddress = null)
    {
        _http = new HttpClient(handler ?? buildGuardedHandler(isBlockedAddress));
        _http.Timeout = options.ServerHttpTimeout;
        _maxBytes = options.MaxDownloadBytes;
    }

    // Resolves the host itself and dials that exact IP, so nothing can rebind between the pre-check and the
    // connect; redirects are refused so a vetted public host cannot bounce us to an internal one.
    private static SocketsHttpHandler buildGuardedHandler(Func<IPAddress, bool>? isBlockedAddress)
    {
        SocketsHttpHandler handler = new() { AllowAutoRedirect = false };
        if (isBlockedAddress is null)
        {
            return handler;
        }
        handler.ConnectCallback = async (context, ct) =>
        {
            DnsEndPoint dns = context.DnsEndPoint;
            IReadOnlyList<IPAddress> addresses = IPAddress.TryParse(dns.Host, out IPAddress? literal)
                ? new[] { literal }
                : await Dns.GetHostAddressesAsync(dns.Host, ct);
            foreach (IPAddress address in addresses)
            {
                if (isBlockedAddress(address))
                {
                    continue;
                }
                Socket socket = new(address.AddressFamily, SocketType.Stream, ProtocolType.Tcp)
                {
                    NoDelay = true,
                };
                try
                {
                    await socket.ConnectAsync(new IPEndPoint(address, dns.Port), ct);
                    return new NetworkStream(socket, ownsSocket: true);
                }
                catch
                {
                    socket.Dispose();
                }
            }
            throw new InvalidOperationException(
                "That link resolves to a private or local address, which isn't allowed.");
        };
        return handler;
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
        catch (HttpRequestException ex) when (blockedAddressCause(ex) is InvalidOperationException blocked)
        {
            // The guard's verbatim message (SafeAsync shows it) rather than the transport error
            // wrapping it.
            throw blocked;
        }
        using (response)
        {
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
    }

    private static InvalidOperationException? blockedAddressCause(Exception ex)
    {
        for (Exception? e = ex.InnerException; e is not null; e = e.InnerException)
        {
            if (e is InvalidOperationException blocked)
            {
                return blocked;
            }
        }
        return null;
    }

    public void Dispose() => _http.Dispose();
}
