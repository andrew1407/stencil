namespace Stencil.TelegramBot.Infrastructure.Server;

// A port of pystencil's normalize_url (itself a port of connectionManager.js normalizeUrl).
public static class UrlNormalizer
{
    // Keeps only scheme://authority. Secure by default: a bare REMOTE host gets https://, loopback
    // keeps http://; an explicit scheme is preserved — the user opts into cleartext.
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

    // localhost, *.localhost, 127.0.0.0/8, ::1 — port of connectionManager.js isLoopbackHost.
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
