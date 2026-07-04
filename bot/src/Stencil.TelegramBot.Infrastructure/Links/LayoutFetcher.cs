using System.Net;
using System.Net.Sockets;
using Stencil.TelegramBot.Infrastructure.Configuration;

namespace Stencil.TelegramBot.Infrastructure.Links;

/// <summary>
/// Small guarded HTTP GET for <c>/layout &lt;url&gt;</c>: fetches a layout JSON body, bounded
/// by the same download cap as Telegram file downloads. Callers SSRF-vet the URL first
/// (<c>RemoteImageUrl.ValidateAsync</c>); this class then re-checks the resolved address at
/// connect time (see the ctor) so a DNS rebind can't slip a private/metadata host past the
/// pre-check.
/// </summary>
public sealed class LayoutFetcher : IDisposable
{
    private readonly HttpClient _http;
    private readonly long _maxBytes;

    /// <summary>
    /// <paramref name="handler"/> lets tests inject a canned <see cref="HttpMessageHandler"/>
    /// (which then bypasses the connect-time guard). In production it is left null and
    /// <paramref name="isBlockedAddress"/> — the same predicate the pre-check uses
    /// (<c>RemoteImageUrl.IsBlockedAddress</c>) — is enforced on the address we actually dial.
    /// </summary>
    public LayoutFetcher(
        BotOptions options,
        HttpMessageHandler? handler = null,
        Func<IPAddress, bool>? isBlockedAddress = null)
    {
        _http = new HttpClient(handler ?? BuildGuardedHandler(isBlockedAddress));
        _http.Timeout = options.ServerHttpTimeout;
        _maxBytes = options.MaxDownloadBytes;
    }

    /// <summary>
    /// A handler that refuses redirects and, when a guard is supplied, resolves the host itself
    /// and connects only to a non-blocked address — closing the DNS-rebinding window between the
    /// caller's pre-check and this fetch (the host could resolve to a public IP during validation
    /// and a private / link-local / cloud-metadata one now). We pick the address and dial that
    /// exact IP, so nothing can rebind between the check and the connect. Redirects are refused
    /// for the same reason: a vetted public host must not bounce us to an internal one.
    /// </summary>
    private static SocketsHttpHandler BuildGuardedHandler(Func<IPAddress, bool>? isBlockedAddress)
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

    /// <summary>
    /// GET the body, or null on a non-success status (redirects included — see the ctor).
    /// Throws when the body exceeds the download cap, or (as <see cref="InvalidOperationException"/>)
    /// when the connect-time guard rejects the resolved address.
    /// </summary>
    public async Task<byte[]?> FetchAsync(string url, CancellationToken ct = default)
    {
        HttpResponseMessage response;
        try
        {
            response = await _http.GetAsync(url, HttpCompletionOption.ResponseHeadersRead, ct);
        }
        catch (HttpRequestException ex) when (BlockedAddressCause(ex) is InvalidOperationException blocked)
        {
            // Surface the guard's verbatim message (SafeAsync shows it to the user) rather
            // than the transport error HttpClient wrapped it in.
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

    /// <summary>The <see cref="InvalidOperationException"/> our connect guard threw, if this
    /// transport failure was caused by it; null otherwise.</summary>
    private static InvalidOperationException? BlockedAddressCause(Exception ex)
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
