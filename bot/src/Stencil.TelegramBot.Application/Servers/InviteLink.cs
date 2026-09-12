namespace Stencil.TelegramBot.Application.Servers;

// <server-url>#token=<session-token>, the shared format the CLI and pystencil connect paths accept.
public static class InviteLink
{
    private const string _marker = "#token=";

    // An explicit token wins over the fragment's.
    public static (string Url, string? Token) Split(string? url, string? token)
    {
        string s = url ?? "";
        int i = s.IndexOf(_marker, StringComparison.Ordinal);
        if (i < 0)
        {
            return (s, token);
        }
        string fragment = s[(i + _marker.Length)..].Trim();
        string? effective = !string.IsNullOrEmpty(token)
            ? token
            : (fragment.Length != 0 ? fragment : null);
        return (s[..i], effective);
    }
}
