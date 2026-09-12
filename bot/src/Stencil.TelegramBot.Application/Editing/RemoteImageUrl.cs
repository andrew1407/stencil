using System.Net;
using System.Net.Sockets;

namespace Stencil.TelegramBot.Application.Editing;

// The trust boundary for a user-supplied /url source: the bot is reachable by any Telegram user, so
// only http(s) URLs whose hosts resolve to publicly routable addresses reach the CLI (SSRF /
// local-file read). The CLI re-resolves in its own process, so DNS rebinding is left to its scheme
// guard and ffmpeg allow-list.
public static class RemoteImageUrl
{
    private static readonly TimeSpan DefaultResolveTimeout = TimeSpan.FromSeconds(5);

    // Throws InvalidOperationException (surfaced verbatim), which also rejects bare local paths and
    // other schemes.
    public static Uri Parse(string raw)
    {
        if (string.IsNullOrWhiteSpace(raw)
            || !Uri.TryCreate(raw.Trim(), UriKind.Absolute, out Uri? uri)
            || (uri.Scheme != Uri.UriSchemeHttp && uri.Scheme != Uri.UriSchemeHttps))
        {
            throw new InvalidOperationException("Only http(s) image links are supported.");
        }
        return uri;
    }

    // Resolution is bounded by resolveTimeout so a hung resolver can't stall the update handler.
    public static async Task ValidateAsync(string raw, CancellationToken ct = default, TimeSpan? resolveTimeout = null)
    {
        Uri uri = Parse(raw);
        IReadOnlyList<IPAddress> addresses;
        if (IPAddress.TryParse(uri.Host, out IPAddress? literal))
        {
            addresses = new[] { literal };
        }
        else
        {
            addresses = await ResolveAsync(uri.Host, resolveTimeout ?? DefaultResolveTimeout, ct);
        }
        if (addresses.Count == 0 || addresses.Any(IsBlockedAddress))
        {
            throw new InvalidOperationException(
                "That link resolves to a private or local address, which isn't allowed.");
        }
    }

    // The /connect guard deliberately ALLOWS loopback and private-LAN servers and blocks only the
    // ranges with no legitimate server use: link-local (169.254.0.0/16 holds the cloud-metadata
    // endpoint; fe80::/10), unspecified and multicast. A bare host is treated as http://, matching
    // connection normalisation.
    public static async Task ValidateServerUrlAsync(string raw, CancellationToken ct = default, TimeSpan? resolveTimeout = null)
    {
        string s = (raw ?? "").Trim();
        if (s.Length == 0)
        {
            throw new InvalidOperationException("Server URL is required.");
        }
        if (!s.StartsWith("http://", StringComparison.OrdinalIgnoreCase)
            && !s.StartsWith("https://", StringComparison.OrdinalIgnoreCase))
        {
            s = "http://" + s;
        }
        if (!Uri.TryCreate(s, UriKind.Absolute, out Uri? uri) || string.IsNullOrEmpty(uri.Host))
        {
            throw new InvalidOperationException($"Invalid server URL: {raw}");
        }
        IReadOnlyList<IPAddress> addresses;
        if (IPAddress.TryParse(uri.Host, out IPAddress? literal))
        {
            addresses = new[] { literal };
        }
        else
        {
            try
            {
                addresses = await ResolveAsync(uri.Host, resolveTimeout ?? DefaultResolveTimeout, ct);
            }
            catch (InvalidOperationException)
            {
                // An unresolvable host is not an SSRF target: only a host proven link-local is
                // rejected.
                return;
            }
        }
        if (addresses.Any(IsCloudMetadataOrLinkLocal))
        {
            throw new InvalidOperationException(
                "That server address isn't allowed (link-local / cloud-metadata range).");
        }
    }

    // A resolver past timeout reads as unreachable; a real caller cancellation propagates.
    private static async Task<IReadOnlyList<IPAddress>> ResolveAsync(string host, TimeSpan timeout, CancellationToken ct)
    {
        using CancellationTokenSource linked = CancellationTokenSource.CreateLinkedTokenSource(ct);
        linked.CancelAfter(timeout);
        try
        {
            return await Dns.GetHostAddressesAsync(host, linked.Token);
        }
        catch (OperationCanceledException) when (!ct.IsCancellationRequested)
        {
            throw new InvalidOperationException($"Timed out resolving host '{host}'.");
        }
        catch (SocketException)
        {
            throw new InvalidOperationException($"Could not resolve host '{host}'.");
        }
    }

    // Loopback, link-local (incl. 169.254.169.254), private/ULA, carrier-grade NAT, multicast and
    // the unspecified address. IPv4-mapped IPv6 is unwrapped first.
    public static bool IsBlockedAddress(IPAddress address)
    {
        IPAddress ip = address.IsIPv4MappedToIPv6 ? address.MapToIPv4() : address;
        if (IPAddress.IsLoopback(ip))
        {
            return true;
        }
        if (ip.AddressFamily == AddressFamily.InterNetworkV6)
        {
            return ip.IsIPv6LinkLocal
                || ip.IsIPv6SiteLocal
                || ip.IsIPv6UniqueLocal
                || ip.IsIPv6Multicast
                || ip.Equals(IPAddress.IPv6Any);
        }
        byte[] b = ip.GetAddressBytes();
        if (b[0] is 0 or 10 or 127)
        {
            return true; // "this" network, 10.0.0.0/8 private, 127.0.0.0/8 loopback
        }
        if (b[0] == 169 && b[1] == 254)
        {
            return true; // 169.254.0.0/16 link-local (cloud metadata endpoint lives here)
        }
        if (b[0] == 172 && b[1] >= 16 && b[1] <= 31)
        {
            return true; // 172.16.0.0/12 private
        }
        if (b[0] == 192 && b[1] == 168)
        {
            return true; // 192.168.0.0/16 private
        }
        if (b[0] == 100 && b[1] >= 64 && b[1] <= 127)
        {
            return true; // 100.64.0.0/10 carrier-grade NAT
        }
        if (b[0] >= 224)
        {
            return true; // 224.0.0.0/4 multicast + 240.0.0.0/4 reserved
        }
        return false;
    }

    // Link-local, unspecified and multicast/reserved only; loopback and private ranges are allowed
    // server targets. IPv4-mapped IPv6 is unwrapped first.
    public static bool IsCloudMetadataOrLinkLocal(IPAddress address)
    {
        IPAddress ip = address.IsIPv4MappedToIPv6 ? address.MapToIPv4() : address;
        if (ip.AddressFamily == AddressFamily.InterNetworkV6)
        {
            return ip.IsIPv6LinkLocal || ip.IsIPv6Multicast || ip.Equals(IPAddress.IPv6Any);
        }
        byte[] b = ip.GetAddressBytes();
        if (b[0] == 169 && b[1] == 254)
        {
            return true; // 169.254.0.0/16 link-local (cloud metadata endpoint lives here)
        }
        if (b[0] == 0)
        {
            return true; // 0.0.0.0/8 "this" network / unspecified
        }
        if (b[0] >= 224)
        {
            return true; // 224.0.0.0/4 multicast + 240.0.0.0/4 reserved
        }
        return false;
    }
}
