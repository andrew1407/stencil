namespace Stencil.TelegramBot.Infrastructure.Server;

/// <summary>
/// Normalise a raw server URL to a stable origin (<c>scheme://host[:port]</c>). A faithful
/// port of <c>pystencil</c>'s <c>normalize_url</c> (itself a port of the browser
/// <c>connectionManager.js</c> <c>normalizeUrl</c>).
/// </summary>
public static class UrlNormalizer
{
    /// <summary>
    /// Trim <paramref name="raw"/>; then keep only <c>scheme://authority</c> (drop any path,
    /// query or fragment) so every connection is keyed by a stable origin. Secure by default:
    /// a bare REMOTE host gets <c>https://</c>; loopback keeps <c>http://</c> (dev servers run
    /// plaintext on localhost). An explicit scheme is preserved — the user opts into cleartext.
    /// Throws <see cref="ArgumentException"/> on empty or invalid input.
    /// </summary>
    public static string Normalize(string? raw)
    {
        string s = (raw ?? "").Trim();
        if (s.Length == 0)
        {
            throw new ArgumentException("Server URL is required");
        }
        if (!s.StartsWith("http://", StringComparison.OrdinalIgnoreCase)
            && !s.StartsWith("https://", StringComparison.OrdinalIgnoreCase))
        {
            string host = Uri.TryCreate("http://" + s, UriKind.Absolute, out Uri? probe)
                ? probe.Host
                : "";
            s = (IsLoopbackHost(host) ? "http://" : "https://") + s;
        }
        if (!Uri.TryCreate(s, UriKind.Absolute, out Uri? uri) || string.IsNullOrEmpty(uri.Authority))
        {
            throw new ArgumentException($"Invalid server URL: {raw}");
        }
        return $"{uri.Scheme}://{uri.Authority}";
    }

    /// <summary>
    /// True for a loopback host (localhost, *.localhost, 127.0.0.0/8, ::1), where plaintext
    /// http is safe because the bytes never leave the machine. Port of the browser
    /// <c>connectionManager.js</c> <c>isLoopbackHost</c>.
    /// </summary>
    public static bool IsLoopbackHost(string? host)
    {
        if (string.IsNullOrEmpty(host))
        {
            return false;
        }
        string h = host.ToLowerInvariant().Trim('[', ']');
        if (h == "localhost" || h.EndsWith(".localhost", StringComparison.Ordinal))
        {
            return true;
        }
        if (h == "::1")
        {
            return true;
        }
        string[] parts = h.Split('.');
        return parts.Length == 4 && parts[0] == "127"
            && parts.Skip(1).All(p => p.Length is >= 1 and <= 3 && p.All(char.IsAsciiDigit));
    }
}
