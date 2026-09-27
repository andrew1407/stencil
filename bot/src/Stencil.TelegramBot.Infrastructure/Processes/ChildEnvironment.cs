namespace Stencil.TelegramBot.Infrastructure.Processes;

// What a child (the stencil CLI, ffmpeg) may inherit: an allowlist, so the bot's own secrets (the
// Telegram token, REDIS_URL, STENCIL_LLM_*) never reach it. Names match in any case, as proxies do.
public static class ChildEnvironment
{
    private static readonly HashSet<string> _allowed = new(StringComparer.OrdinalIgnoreCase)
    {
        "PATH", "HOME", "TMPDIR", "TMP", "TEMP", "LANG", "LANGUAGE", "TZ", "NO_COLOR", "DEVELOPER_DIR",
        "HTTP_PROXY", "HTTPS_PROXY", "NO_PROXY", "ALL_PROXY", "SSL_CERT_FILE", "SSL_CERT_DIR",
        "LD_LIBRARY_PATH",
        // A Windows process cannot start without these.
        "SYSTEMROOT", "WINDIR", "COMSPEC", "PATHEXT", "USERPROFILE", "APPDATA", "LOCALAPPDATA",
    };

    public static bool Allows(string name) =>
        _allowed.Contains(name) || name.StartsWith("LC_", StringComparison.OrdinalIgnoreCase);

    // Strips in place: ProcessStartInfo.Environment starts as a copy of the bot's own environment.
    public static void Restrict(IDictionary<string, string?> environment)
    {
        foreach (string name in environment.Keys.Where(name => !Allows(name)).ToList())
        {
            environment.Remove(name);
        }
    }
}
