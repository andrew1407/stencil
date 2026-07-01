using System.Net;
using System.Net.Sockets;

namespace Stencil.TelegramBot.Application.Editing;

/// <summary>
/// Validates a user-supplied <c>/url</c> image source before it is handed to the CLI to fetch.
/// The bot is reachable by any Telegram user, so this is the trust boundary that keeps the
/// CLI's "load from URL/path" capability from being turned into a server-side request forgery
/// (SSRF) or a local-file read: only <c>http(s)</c> URLs are accepted, and hosts that resolve
/// to loopback / link-local / private / carrier-grade / cloud-metadata addresses are rejected.
/// </summary>
/// <remarks>
/// The CLI re-resolves and fetches in a separate process, so this cannot by itself close a
/// DNS-rebinding window; the CLI's own scheme guard and ffmpeg protocol allow-list are the
/// defence-in-depth backstop. What this reliably blocks is the practical attack: a literal
/// internal/metadata host, a bare local path (LFI), or a non-http scheme.
/// </remarks>
public static class RemoteImageUrl
{
    /// <summary>How long host resolution may take before the link is rejected as unreachable.</summary>
    private static readonly TimeSpan DefaultResolveTimeout = TimeSpan.FromSeconds(5);

    /// <summary>
    /// Parse and require an absolute <c>http</c>/<c>https</c> URL. Throws
    /// <see cref="InvalidOperationException"/> (surfaced verbatim to the user) otherwise —
    /// which also rejects bare local paths (e.g. <c>/etc/passwd.png</c>) and other schemes
    /// (<c>file:</c>, <c>ftp:</c>, …).
    /// </summary>
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

    /// <summary>
    /// Validate the URL and confirm every address its host resolves to is publicly routable.
    /// Host resolution is bounded by <paramref name="resolveTimeout"/> (default 5s) so a slow or
    /// hung resolver can't stall the calling update handler.
    /// </summary>
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

    /// <summary>
    /// Validate a user-supplied <c>/connect</c> server origin. Unlike <see cref="ValidateAsync"/>
    /// this deliberately <em>allows</em> loopback and private-LAN targets — connecting to a
    /// collaboration server on localhost or the LAN is an intended feature — and blocks only the
    /// ranges that have no legitimate use as a server target and are the real SSRF danger:
    /// link-local (the <c>169.254.169.254</c> cloud-metadata endpoint lives in
    /// <c>169.254.0.0/16</c>; IPv6 <c>fe80::/10</c>), plus the unspecified and multicast ranges.
    /// A bare host (no scheme) is treated as <c>http://</c>, matching how connections are normalised.
    /// Host resolution is bounded so a hung resolver can't stall the calling update handler.
    /// </summary>
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
                // A host that won't resolve (or a slow resolver) can't be reached anyway, so it is
                // not an SSRF target: unlike the /url guard we let the connection attempt proceed
                // and fail on its own. We only ever reject a host we can prove is link-local.
                return;
            }
        }
        if (addresses.Any(IsCloudMetadataOrLinkLocal))
        {
            throw new InvalidOperationException(
                "That server address isn't allowed (link-local / cloud-metadata range).");
        }
    }

    /// <summary>
    /// Resolve a host with a timeout. A resolver that exceeds <paramref name="timeout"/> is
    /// reported as unreachable (distinct from a real caller cancellation, which propagates).
    /// </summary>
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

    /// <summary>
    /// True for addresses that must never be reachable from a user-supplied link: loopback,
    /// link-local (incl. the 169.254.169.254 metadata address), private/ULA, carrier-grade
    /// NAT, multicast, and the unspecified address. IPv4-mapped IPv6 is unwrapped first.
    /// </summary>
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

    /// <summary>
    /// The narrow guard for <c>/connect</c> targets: true only for addresses that are never a
    /// legitimate collaboration server yet are the SSRF danger — link-local (incl. the
    /// <c>169.254.169.254</c> cloud-metadata endpoint in <c>169.254.0.0/16</c> and IPv6
    /// <c>fe80::/10</c>), the unspecified address, and multicast/reserved. Loopback and
    /// private/carrier-grade ranges are deliberately <em>not</em> blocked here (they are allowed
    /// server targets). IPv4-mapped IPv6 is unwrapped first, mirroring <see cref="IsBlockedAddress"/>.
    /// </summary>
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
