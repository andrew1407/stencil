namespace Stencil.TelegramBot.Infrastructure.Configuration;

/// <summary>
/// A tiny <c>.env</c> loader: parse <c>KEY=VALUE</c> lines and export each into the process
/// environment, but only when the variable is not already set (the real environment always
/// wins). Mirrors the dev-time convenience the Go server documents with <c>server/.env.example</c>.
/// </summary>
public static class DotEnv
{
    /// <summary>
    /// Load <paramref name="path"/> if it exists, setting each parsed key via
    /// <see cref="Environment.SetEnvironmentVariable(string, string)"/> only when that
    /// variable is currently unset in the real environment. A missing file is a no-op.
    /// </summary>
    public static void Load(string path)
    {
        if (!File.Exists(path))
        {
            return;
        }
        string text = File.ReadAllText(path);
        foreach (KeyValuePair<string, string> pair in Parse(text))
        {
            string? existing = Environment.GetEnvironmentVariable(pair.Key);
            if (existing is not null)
            {
                continue;
            }
            Environment.SetEnvironmentVariable(pair.Key, pair.Value);
        }
    }

    /// <summary>
    /// Parse <c>.env</c> text into a key→value map. Blank lines and <c>#</c> comments are
    /// skipped; keys/values are trimmed; a leading <c>export </c> is stripped; and matching
    /// surrounding single or double quotes are removed from the value. Pure and unit-testable.
    /// </summary>
    public static IReadOnlyDictionary<string, string> Parse(string text)
    {
        Dictionary<string, string> result = new();
        string[] lines = text.Replace("\r\n", "\n").Replace('\r', '\n').Split('\n');
        foreach (string raw in lines)
        {
            string line = raw.Trim();
            if (line.Length == 0)
            {
                continue;
            }
            if (line.StartsWith('#'))
            {
                continue;
            }
            if (line.StartsWith("export ", StringComparison.Ordinal))
            {
                line = line["export ".Length..].TrimStart();
            }
            int eq = line.IndexOf('=');
            if (eq <= 0)
            {
                continue;
            }
            string key = line[..eq].Trim();
            if (key.Length == 0)
            {
                continue;
            }
            string value = line[(eq + 1)..].Trim();
            value = StripQuotes(value);
            result[key] = value;
        }
        return result;
    }

    /// <summary>Drop one matching pair of surrounding single or double quotes.</summary>
    private static string StripQuotes(string value)
    {
        if (value.Length < 2)
        {
            return value;
        }
        char first = value[0];
        char last = value[^1];
        bool quoted = (first == '"' && last == '"') || (first == '\'' && last == '\'');
        if (quoted)
        {
            return value[1..^1];
        }
        return value;
    }
}
