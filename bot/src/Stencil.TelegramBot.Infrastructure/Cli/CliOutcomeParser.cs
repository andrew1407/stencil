using Stencil.TelegramBot.Domain.Editing;

namespace Stencil.TelegramBot.Infrastructure.Cli;

// A port of mcp/src/outcome.rs over the cli/CONTRACT.md §2 stderr contract, so the shared
// cli/testdata/outcome_fixtures.json pass identically. NO_COLOR=1 keeps ANSI out.
public static partial class CliOutcomeParser
{
    // The exact stderr prefixes the CLI emits — the peer of mcp's PREFIX_* consts.
    private const string _prefixWrote = "wrote ";
    private const string _prefixUpdated = "updated server result for project ";
    private const string _prefixCreated = "created server project ";
    private const string _prefixError = "error:";
    private const string _suffixProject = " (project)";
    private const string _prefixScraped = "scraped ";
    private const string _intoToken = " into ";

    // Reverse-searches " (" so a path containing it still parses; mirrors mcp's parse_wrote.
    public static RenderResult? ParseWrote(string stderr)
    {
        foreach (string rawLine in splitLines(stderr))
        {
            string line = rawLine.Trim();
            if (!line.StartsWith(_prefixWrote, StringComparison.Ordinal))
            {
                continue;
            }
            string rest = line[_prefixWrote.Length..];
            int open = rest.LastIndexOf(" (", StringComparison.Ordinal);
            if (open < 0)
            {
                continue;
            }
            string path = rest[..open];
            string tail = rest[(open + 2)..];
            if (!tail.EndsWith(')'))
            {
                continue;
            }
            tail = tail[..^1];
            // The dims lead; newer builds append " px · {page}" metadata (cm uses '×', not 'x').
            string? dims = firstWhitespaceToken(tail);
            if (dims is null)
            {
                continue;
            }
            if (tryParseWxH(dims, out int width, out int height))
            {
                return new RenderResult(path, width, height);
            }
        }
        return null;
    }

    // A project is a document, so the CLI reports no dimensions (which is why ParseWrote skips it).
    public static string? ParseWroteProject(string stderr)
    {
        foreach (string rawLine in splitLines(stderr))
        {
            string line = rawLine.Trim();
            if (line.StartsWith(_prefixWrote, StringComparison.Ordinal)
                && line.EndsWith(_suffixProject, StringComparison.Ordinal))
            {
                return line[_prefixWrote.Length..^_suffixProject.Length];
            }
        }
        return null;
    }

    // In order — one call can both update and create; mirrors mcp's parse_remotes.
    public static IReadOnlyList<RemoteDelivery> ParseRemotes(string stderr)
    {
        List<RemoteDelivery> result = new();
        foreach (string rawLine in splitLines(stderr))
        {
            string line = rawLine.Trim();
            if (line.StartsWith(_prefixUpdated, StringComparison.Ordinal))
            {
                // `{id} ({w}x{h})` — rfind " (" so an id can't be confused with the dims.
                string rest = line[_prefixUpdated.Length..];
                int open = rest.LastIndexOf(" (", StringComparison.Ordinal);
                if (open < 0)
                {
                    continue;
                }
                string id = rest[..open];
                string dimsTail = rest[(open + 2)..];
                if (!dimsTail.EndsWith(')'))
                {
                    continue;
                }
                string dims = dimsTail[..^1];
                if (tryParseWxH(dims, out int width, out int height))
                {
                    result.Add(new RemoteDelivery.Updated(id, width, height));
                }
            }
            else if (line.StartsWith(_prefixCreated, StringComparison.Ordinal))
            {
                // `"{name}" ({id})` — the id is the parenthesised tail; the name is quoted.
                string rest = line[_prefixCreated.Length..];
                int open = rest.LastIndexOf(" (", StringComparison.Ordinal);
                if (open < 0)
                {
                    continue;
                }
                string idTail = rest[(open + 2)..];
                if (!idTail.EndsWith(')'))
                {
                    continue;
                }
                string id = idTail[..^1];
                string name = rest[..open].Trim().Trim('"');
                result.Add(new RemoteDelivery.Created(name, id));
            }
        }
        return result;
    }

    // Mirrors mcp's extract_errors.
    public static string ExtractErrors(string stderr)
    {
        List<string> errors = new();
        foreach (string rawLine in splitLines(stderr))
        {
            string line = rawLine.Trim();
            if (line.StartsWith(_prefixError, StringComparison.Ordinal))
            {
                errors.Add(line);
            }
        }
        if (errors.Count == 0)
        {
            string trimmed = stderr.Trim();
            if (trimmed.Length == 0)
            {
                return "the stencil CLI failed without a message";
            }
            return trimmed;
        }
        return string.Join("\n", errors);
    }

    // Splits on the first ASCII 'x' (cm uses '×').
    private static bool tryParseWxH(string dims, out int width, out int height)
    {
        width = height = 0;
        int x = dims.IndexOf('x');
        return x >= 0
            && int.TryParse(dims[..x].Trim(), out width)
            && int.TryParse(dims[(x + 1)..].Trim(), out height);
    }

    private static string? firstWhitespaceToken(string text)
    {
        string[] tokens = text.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
        return tokens.Length == 0 ? null : tokens[0];
    }

    private static IEnumerable<string> splitLines(string text) =>
        text.Replace("\r\n", "\n").Replace('\r', '\n').Split('\n');
}
