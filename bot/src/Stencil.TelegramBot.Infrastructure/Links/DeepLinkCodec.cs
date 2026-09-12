using System.Text;
using Stencil.TelegramBot.Infrastructure.Server;

namespace Stencil.TelegramBot.Infrastructure.Links;

// The Telegram ?start= payload for "Open in…": "1" + base64url("host[:port]|projectId"), padding
// stripped, capped at Telegram's 64 chars. The scheme is kept only when it is NOT what
// UrlNormalizer would infer. The identical codec lives in browser/js/core/deepLink.js and
// desktop/src/app/deepLink.cpp — keep all three in sync.
public static class DeepLinkCodec
{
    // Telegram's cap, charset [A-Za-z0-9_-].
    public const int TELEGRAM_START_LIMIT = 64;

    // Null when over the 64-char limit — callers then fall back to copyable /connect + /fetch
    // commands.
    public static string? Encode(string serverUrl, string projectId)
    {
        string origin = UrlNormalizer.Normalize(serverUrl);
        string plain = $"{compressOrigin(origin)}|{projectId}";
        string payload = "1" + Convert.ToBase64String(Encoding.UTF8.GetBytes(plain))
            .Replace('+', '-')
            .Replace('/', '_')
            .TrimEnd('=');
        return payload.Length <= TELEGRAM_START_LIMIT ? payload : null;
    }

    // False for anything but a well-formed version-1 payload — a plain /start greeting then
    // applies.
    public static bool TryDecode(string? payload, out string serverUrl, out string projectId)
    {
        serverUrl = "";
        projectId = "";
        string p = (payload ?? "").Trim();
        if (p.Length < 2 || p[0] != '1' || p.Length > TELEGRAM_START_LIMIT
            || p.Skip(1).Any(c => !isBase64UrlChar(c)))
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

    private static string compressOrigin(string origin)
    {
        Uri uri = new(origin);
        string defaultScheme = UrlNormalizer.IsLoopbackHost(uri.Host) ? "http" : "https";
        return uri.Scheme == defaultScheme ? uri.Authority : origin;
    }

    private static bool isBase64UrlChar(char c) =>
        char.IsAsciiLetterOrDigit(c) || c is '-' or '_';
}
