using System.Text;
using Stencil.TelegramBot.Infrastructure.Server;

namespace Stencil.TelegramBot.Infrastructure.Links;

/// <summary>
/// The Telegram <c>?start=</c> deep-link payload codec for the cross-front-end "Open in…"
/// feature: <c>"1" + base64url("host[:port]|projectId")</c>, padding stripped, capped at
/// Telegram's 64-char limit. The scheme is kept only when it is NOT what
/// <see cref="UrlNormalizer"/> would infer for the bare host (https for remote, http for
/// loopback) — decoding re-normalizes, so the default scheme round-trips from just
/// <c>host[:port]</c>.
/// </summary>
/// <remarks>
/// The identical codec exists in <c>browser/js/core/deepLink.js</c> and
/// <c>desktop/src/app/deepLink.cpp</c> — keep the three in sync (shared golden vectors in
/// each suite's tests: <c>DeepLinkCodecTests</c> here).
/// </remarks>
public static class DeepLinkCodec
{
    /// <summary>Telegram caps start payloads at 64 chars from the charset [A-Za-z0-9_-].</summary>
    public const int TelegramStartLimit = 64;

    /// <summary>
    /// Encode (server url, project id) into a start payload, or null when the result would
    /// exceed the 64-char limit — callers must then fall back to showing copyable
    /// <c>/connect &lt;url&gt;</c> + <c>/fetch &lt;id&gt;</c> commands.
    /// </summary>
    public static string? Encode(string serverUrl, string projectId)
    {
        string origin = UrlNormalizer.Normalize(serverUrl);
        string plain = $"{CompressOrigin(origin)}|{projectId}";
        string payload = "1" + Convert.ToBase64String(Encoding.UTF8.GetBytes(plain))
            .Replace('+', '-')
            .Replace('/', '_')
            .TrimEnd('=');
        return payload.Length <= TelegramStartLimit ? payload : null;
    }

    /// <summary>
    /// Decode a start payload into (normalized server origin, project id). False for anything
    /// that isn't a well-formed version-1 payload — a plain <c>/start</c> greeting then applies.
    /// </summary>
    public static bool TryDecode(string? payload, out string serverUrl, out string projectId)
    {
        serverUrl = "";
        projectId = "";
        string p = (payload ?? "").Trim();
        if (p.Length < 2 || p[0] != '1' || p.Length > TelegramStartLimit
            || p.Skip(1).Any(c => !IsBase64UrlChar(c)))
        {
            return false;
        }
        string b64 = p[1..].Replace('-', '+').Replace('_', '/');
        b64 = b64.PadRight(b64.Length + (4 - b64.Length % 4) % 4, '=');
        string plain;
        try
        {
            plain = Encoding.UTF8.GetString(Convert.FromBase64String(b64));
        }
        catch (FormatException)
        {
            return false;
        }
        int pipe = plain.IndexOf('|');
        if (pipe <= 0 || pipe == plain.Length - 1)
        {
            return false;
        }
        try
        {
            serverUrl = UrlNormalizer.Normalize(plain[..pipe]);
        }
        catch (ArgumentException)
        {
            return false;
        }
        projectId = plain[(pipe + 1)..];
        return true;
    }

    /// <summary>Drop the scheme when it matches the normalize default for the bare host.</summary>
    private static string CompressOrigin(string origin)
    {
        Uri uri = new(origin);
        string defaultScheme = UrlNormalizer.IsLoopbackHost(uri.Host) ? "http" : "https";
        return uri.Scheme == defaultScheme ? uri.Authority : origin;
    }

    private static bool IsBase64UrlChar(char c) =>
        char.IsAsciiLetterOrDigit(c) || c is '-' or '_';
}
