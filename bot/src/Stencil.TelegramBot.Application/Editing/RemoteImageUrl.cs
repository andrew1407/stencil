using System.Net;
using System.Net.Sockets;
using Stencil.TelegramBot.Domain.Configuration;

namespace Stencil.TelegramBot.Application.Editing;

// The trust boundary for a user-supplied /url: only http(s) URLs whose hosts resolve to publicly
// routable addresses reach the CLI (SSRF / local-file read); the CLI re-resolves in its own process.
public static class RemoteImageUrl
{
    // For a caller without a policy.
    public static readonly TimeSpan DefaultResolveTimeout = TimeSpan.FromSeconds(IBotPolicy.DEFAULT_RESOLVE_TIMEOUT_SECONDS);

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
            addresses = await resolveAsync(uri.Host, resolveTimeout ?? DefaultResolveTimeout, ct);
        }
        if (addresses.Count == 0 || addresses.Any(IsBlockedAddress))
        {
            throw new InvalidOperationException(
                "That link resolves to a private or local address, which isn't allowed.");
        }
    }

    // /connect ALLOWS loopback and private-LAN servers unless allowPrivate is off; link-local, cloud
    // metadata, unspecified, multicast and reserved are always refused. A bare host is http://.
    public static async Task ValidateServerUrlAsync(string raw, CancellationToken ct = default,
        TimeSpan? resolveTimeout = null, bool allowPrivate = true)
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
                addresses = await resolveAsync(uri.Host, resolveTimeout ?? DefaultResolveTimeout, ct);
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
        if (!allowPrivate && addresses.Any(isBlockedPublicServer))
        {
            throw new InvalidOperationException(PRIVATE_SERVER_REFUSAL);
        }
    }

    public const string PRIVATE_SERVER_REFUSAL =
        "That server address isn't allowed (this bot connects only to public servers).";

    // ValidateServerUrlAsync's verdict for the address actually dialled, so a host can't rebind past it.
    public static Func<IPAddress, bool> ServerAddressGuard(bool allowPrivate) =>
        allowPrivate ? IsCloudMetadataOrLinkLocal : isBlockedPublicServer;

    // A resolver past timeout reads as unreachable; a real caller cancellation propagates.
    private static async Task<IReadOnlyList<IPAddress>> resolveAsync(string host, TimeSpan timeout, CancellationToken ct)
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

    public static bool IsBlockedAddress(IPAddress address) =>
        AddressRanges.Blocks(AddressRanges.FETCH, address);

    // Loopback and private ranges are allowed server targets.
    public static bool IsCloudMetadataOrLinkLocal(IPAddress address) =>
        AddressRanges.Blocks(AddressRanges.SERVER_TARGET, address, AddressRanges.ALLOW_PRIVATE);

    private static bool isBlockedPublicServer(IPAddress address) =>
        AddressRanges.Blocks(AddressRanges.SERVER_TARGET, address);
}
