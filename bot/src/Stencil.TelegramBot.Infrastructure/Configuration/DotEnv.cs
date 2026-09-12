namespace Stencil.TelegramBot.Infrastructure.Configuration;

// KEY=VALUE lines exported only when the variable is not already set (the real environment always
// wins).
public static class DotEnv
{
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

    // Blank lines and # comments skipped, a leading `export ` stripped, matching surrounding quotes
    // removed.
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
