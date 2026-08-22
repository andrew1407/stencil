namespace Stencil.TelegramBot.Application.Servers;

/// <summary>
/// Invite-link parsing for the connect path: <c>&lt;server-url&gt;#token=&lt;session-token&gt;</c>,
/// the shared format the CLI and pystencil connect paths also accept.
/// </summary>
public static class InviteLink
{
    private const string Marker = "#token=";

    /// <summary>
    /// Split a <c>#token=</c> fragment off <paramref name="url"/>: the fragment is stripped
    /// and its value becomes the supplied token — unless an explicit
    /// <paramref name="token"/> was passed, which wins.
    /// </summary>
    public static (string Url, string? Token) Split(string? url, string? token)
    {
        string s = url ?? "";
        int i = s.IndexOf(Marker, StringComparison.Ordinal);
        if (i < 0)
        {
            return (s, token);
        }
        string fragment = s[(i + Marker.Length)..].Trim();
        string? effective = !string.IsNullOrEmpty(token)
            ? token
            : (fragment.Length != 0 ? fragment : null);
        return (s[..i], effective);
    }
}
