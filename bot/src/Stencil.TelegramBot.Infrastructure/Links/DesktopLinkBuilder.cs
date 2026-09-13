using System.Globalization;
using System.Text;
using Stencil.TelegramBot.Infrastructure.Server;

namespace Stencil.TelegramBot.Infrastructure.Links;

// Telegram only linkifies http(s), so the stencil://open?… scheme URL rides inside the browser
// app's launch.html bounce page. Port of deepLink.js buildDesktopBounceUrl plus the server branch
// of openIn.js buildStencilSchemeUrl; golden vectors in browser/tests/deepLink.test.js and
// browser-extension/tests/openIn.test.js.
public static class DesktopLinkBuilder
{
    // The browser's desktopScheme.
    public const string DEFAULT_SCHEME = "stencil";

    // No token ever rides it: the receiving client connects with its own credential for that
    // origin.
    public static string SchemeUrl(string serverUrl, string projectId, long version = 0,
        bool incognito = false, string scheme = DEFAULT_SCHEME)
    {
        StringBuilder query = new();
        query.Append("server=").Append(encodeComponent(serverUrl));
        query.Append("&id=").Append(encodeComponent(projectId));
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
        $"{(browserBase ?? "").TrimEnd('/')}/launch.html#stencil-desktop={encodeComponent(stencilUrl)}";

    // Null when the operator configured no http(s) browser app base — the caller then says the link
    // surface is off.
    public static string? TryProjectBounceUrl(string? browserBase, string serverUrl, string projectId,
        long version = 0, bool incognito = false)
    {
        string? origin = normalizeBase(browserBase);
        return origin is null
            ? null
            : BounceUrl(origin, SchemeUrl(serverUrl, projectId, version, incognito));
    }

    // A link to this machine resolves on whoever taps it, not on the operator's host, so the caller
    // says so.
    public static bool IsLoopbackBase(string? browserBase) =>
        normalizeBase(browserBase) is { } origin
        && UrlNormalizer.IsLoopbackHost(new Uri(origin).Host);

    // The path is kept (the app may be served under one, e.g. GitHub Pages), unlike a server
    // origin.
    private static string? normalizeBase(string? browserBase)
    {
        if (string.IsNullOrWhiteSpace(browserBase)
            || !Uri.TryCreate(browserBase.Trim(), UriKind.Absolute, out Uri? uri)
            || (uri.Scheme != Uri.UriSchemeHttp && uri.Scheme != Uri.UriSchemeHttps))
        {
            return null;
        }
        return uri.GetLeftPart(UriPartial.Path).TrimEnd('/');
    }

    // JS encodeURIComponent: Uri.EscapeDataString also escapes !'()*, so put those five back.
    private static string encodeComponent(string value) =>
        Uri.EscapeDataString(value)
            .Replace("%21", "!", StringComparison.Ordinal)
            .Replace("%27", "'", StringComparison.Ordinal)
            .Replace("%28", "(", StringComparison.Ordinal)
            .Replace("%29", ")", StringComparison.Ordinal)
            .Replace("%2A", "*", StringComparison.Ordinal);
}
