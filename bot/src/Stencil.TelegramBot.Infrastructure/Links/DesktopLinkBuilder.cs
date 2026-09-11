using System.Globalization;
using System.Text;
using Stencil.TelegramBot.Infrastructure.Server;

namespace Stencil.TelegramBot.Infrastructure.Links;

/// <summary>
/// Outbound "Open in… → Desktop app" links for a server project: the <c>stencil://open?…</c>
/// scheme URL the desktop app registers with the OS, wrapped in the https bounce URL that can
/// survive a chat message.
/// </summary>
/// <remarks>
/// Telegram only linkifies <c>http(s)</c>, so the scheme URL rides inside the browser app's
/// <c>launch.html</c> bounce page, which validates it and forwards to the scheme. Port of
/// <c>browser/js/core/deepLink.js</c> <c>buildDesktopBounceUrl</c> plus the server branch of
/// <c>buildStencilSchemeUrl</c> (<c>extension/src/lib/openIn.js</c>) — the bot only ever links
/// server PROJECTS, since a chat link can't carry image bytes. Golden vectors:
/// <c>browser/tests/deepLink.test.js</c>, <c>extension/tests/openIn.test.js</c>,
/// <c>DesktopLinkBuilderTests</c> here.
/// </remarks>
public static class DesktopLinkBuilder
{
    /// <summary>The URL scheme the desktop app registers by default (browser <c>desktopScheme</c>).</summary>
    public const string DefaultScheme = "stencil";

    /// <summary>
    /// Build a <c>stencil://open?server=…&amp;id=…[&amp;version=n][&amp;incognito=1]</c> URL. No token
    /// ever rides it: the receiving client connects with its own credential for that origin.
    /// </summary>
    public static string SchemeUrl(string serverUrl, string projectId, long version = 0,
        bool incognito = false, string scheme = DefaultScheme)
    {
        StringBuilder query = new();
        query.Append("server=").Append(EncodeComponent(serverUrl));
        query.Append("&id=").Append(EncodeComponent(projectId));
        if (version > 0)
        {
            query.Append("&version=").Append(version.ToString(CultureInfo.InvariantCulture));
        }
        if (incognito)
        {
            query.Append("&incognito=1");
        }
        return $"{scheme}://open?{query}";
    }

    public static string BounceUrl(string? browserBase, string stencilUrl) =>
        $"{(browserBase ?? "").TrimEnd('/')}/launch.html#stencil-desktop={EncodeComponent(stencilUrl)}";

    /// <summary>
    /// The desktop hand-off link for one server project, or null when the operator has not
    /// configured a browser app base (or configured something that isn't an http(s) URL) — the
    /// caller then explains the link surface is off rather than sending a broken address.
    /// </summary>
    public static string? TryProjectBounceUrl(string? browserBase, string serverUrl, string projectId,
        long version = 0, bool incognito = false)
    {
        string? origin = NormalizeBase(browserBase);
        return origin is null
            ? null
            : BounceUrl(origin, SchemeUrl(serverUrl, projectId, version, incognito));
    }

    /// <summary>
    /// True when the configured base points at this machine — a link the bot hands out then
    /// resolves on whoever taps it, not on the operator's host, so the caller says so.
    /// </summary>
    public static bool IsLoopbackBase(string? browserBase) =>
        NormalizeBase(browserBase) is { } origin
        && UrlNormalizer.IsLoopbackHost(new Uri(origin).Host);

    /// <summary>
    /// The configured base as <c>scheme://authority[/path]</c> with no query, fragment or
    /// trailing slash — the app may be served under a path (GitHub Pages), so unlike a server
    /// origin the path is kept. Null for anything that isn't an absolute http(s) URL.
    /// </summary>
    private static string? NormalizeBase(string? browserBase)
    {
        if (string.IsNullOrWhiteSpace(browserBase)
            || !Uri.TryCreate(browserBase.Trim(), UriKind.Absolute, out Uri? uri)
            || (uri.Scheme != Uri.UriSchemeHttp && uri.Scheme != Uri.UriSchemeHttps))
        {
            return null;
        }
        return uri.GetLeftPart(UriPartial.Path).TrimEnd('/');
    }

    /// <summary>
    /// JS <c>encodeURIComponent</c>: <see cref="Uri.EscapeDataString"/> also escapes
    /// <c>!'()*</c>, so put those five back and the two surfaces emit the same bytes.
    /// </summary>
    private static string EncodeComponent(string value) =>
        Uri.EscapeDataString(value)
            .Replace("%21", "!", StringComparison.Ordinal)
            .Replace("%27", "'", StringComparison.Ordinal)
            .Replace("%28", "(", StringComparison.Ordinal)
            .Replace("%29", ")", StringComparison.Ordinal)
            .Replace("%2A", "*", StringComparison.Ordinal);
}
