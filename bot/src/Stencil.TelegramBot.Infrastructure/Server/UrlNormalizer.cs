namespace Stencil.TelegramBot.Infrastructure.Server;

/// <summary>
/// Normalise a raw server URL to a stable origin (<c>scheme://host[:port]</c>). A faithful
/// port of <c>pystencil</c>'s <c>normalize_url</c> (itself a port of the browser
/// <c>connectionManager.js</c> <c>normalizeUrl</c>).
/// </summary>
public static class UrlNormalizer
{
    /// <summary>
    /// Trim <paramref name="raw"/>; default the scheme to <c>http://</c> when absent; then keep
    /// only <c>scheme://authority</c> (drop any path, query or fragment) so every connection is
    /// keyed by a stable origin. Throws <see cref="ArgumentException"/> on empty or invalid input.
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
            s = "http://" + s;
        }
        if (!Uri.TryCreate(s, UriKind.Absolute, out Uri? uri) || string.IsNullOrEmpty(uri.Authority))
        {
            throw new ArgumentException($"Invalid server URL: {raw}");
        }
        return $"{uri.Scheme}://{uri.Authority}";
    }
}
