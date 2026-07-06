using Stencil.TelegramBot.Domain.Editing;

namespace Stencil.TelegramBot.Infrastructure.Cli;

/// <summary>
/// Parse the CLI's human-readable stderr into structured results. A faithful port of
/// <c>mcp/src/outcome.rs</c> (the reference for the output contract in <c>cli/CONTRACT.md</c>
/// §2). Its parsing semantics match mcp's op-for-op so the shared golden fixtures
/// (<c>cli/testdata/outcome_fixtures.json</c>) pass identically on both sides.
/// </summary>
/// <remarks>
/// The CLI writes everything — banner, usage, errors, and the success line — to <b>stderr</b>
/// (stdout stays empty; the result is a written file). On success it prints exactly one line
/// <c>wrote {path} ({w}x{h} px · {page})</c> (the page suffix is informational; older builds
/// printed a bare <c>({w}x{h})</c>); on failure one or more <c>error: …</c> lines. When
/// <c>--remote-update</c>/<c>--remote</c> are used it also prints server-delivery lines. The
/// child runs with <c>NO_COLOR=1</c> so this text is free of ANSI escapes.
/// </remarks>
public static class CliOutcomeParser
{
    // ── CLI output line prefixes ──
    // The exact stderr markers the CLI (cli/) emits and this module parses — the .NET peer of
    // mcp's PREFIX_* consts (mcp/src/outcome.rs).
    private const string PrefixWrote = "wrote ";
    private const string PrefixUpdated = "updated server result for project ";
    private const string PrefixCreated = "created server project ";
    private const string PrefixError = "error:";

    /// <summary>
    /// Find and parse the <c>wrote {path} ({w}x{h} …)</c> line, or null when absent. Uses a
    /// reverse search for <c>" ("</c> so paths containing <c>" ("</c> still parse, and reads
    /// only the leading whitespace-delimited <c>{w}x{h}</c> token of the parenthesised tail —
    /// mirroring <c>parse_wrote</c> in <c>mcp/src/outcome.rs</c>.
    /// </summary>
    public static RenderResult? ParseWrote(string stderr)
    {
        foreach (string rawLine in SplitLines(stderr))
        {
            string line = rawLine.Trim();
            if (!line.StartsWith(PrefixWrote, StringComparison.Ordinal))
            {
                continue;
            }
            string rest = line[PrefixWrote.Length..];
            // Split off the trailing " (WxH …)" — rfind so paths containing " (" still work.
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
            // The dims are the leading whitespace-delimited token; newer builds append
            // " px · {page}" metadata (the cm size uses '×' U+00D7, never ASCII 'x').
            string? dims = FirstWhitespaceToken(tail);
            if (dims is null)
            {
                continue;
            }
            if (TryParseWxH(dims, out int width, out int height))
            {
                return new RenderResult(path, width, height);
            }
        }
        return null;
    }

    /// <summary>
    /// Parse any collaboration-server delivery line(s) the CLI prints after a successful write.
    /// A single call can both update a fetched project and create a new one, so this returns
    /// all it finds, in order — mirroring <c>parse_remotes</c> in <c>mcp/src/outcome.rs</c>.
    /// </summary>
    public static IReadOnlyList<RemoteDelivery> ParseRemotes(string stderr)
    {
        List<RemoteDelivery> result = new();
        foreach (string rawLine in SplitLines(stderr))
        {
            string line = rawLine.Trim();
            if (line.StartsWith(PrefixUpdated, StringComparison.Ordinal))
            {
                // `{id} ({w}x{h})` — rfind " (" so an id can't be confused with the dims.
                string rest = line[PrefixUpdated.Length..];
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
                if (TryParseWxH(dims, out int width, out int height))
                {
                    result.Add(new RemoteDelivery.Updated(id, width, height));
                }
            }
            else if (line.StartsWith(PrefixCreated, StringComparison.Ordinal))
            {
                // `"{name}" ({id})` — the id is the parenthesised tail; the name is quoted.
                string rest = line[PrefixCreated.Length..];
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

    /// <summary>
    /// Pull the <c>error: …</c> line(s) out of stderr for surfacing back to the caller. Falls
    /// back to the whole trimmed stderr when no <c>error:</c> prefix is found, and to a generic
    /// message when stderr is empty. Mirrors <c>extract_errors</c> in <c>mcp/src/outcome.rs</c>.
    /// </summary>
    public static string ExtractErrors(string stderr)
    {
        List<string> errors = new();
        foreach (string rawLine in SplitLines(stderr))
        {
            string line = rawLine.Trim();
            if (line.StartsWith(PrefixError, StringComparison.Ordinal))
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

    /// <summary>
    /// Parse a <c>{w}x{h}</c> dims token, splitting on the first ASCII <c>'x'</c> (the cm size in
    /// the metadata suffix uses '×' U+00D7, never ASCII 'x'). False when it isn't well-formed.
    /// </summary>
    private static bool TryParseWxH(string dims, out int width, out int height)
    {
        width = height = 0;
        int x = dims.IndexOf('x');
        return x >= 0
            && int.TryParse(dims[..x].Trim(), out width)
            && int.TryParse(dims[(x + 1)..].Trim(), out height);
    }

    /// <summary>
    /// The first whitespace-delimited token of <paramref name="text"/>, or null when it is all
    /// whitespace — the .NET peer of Rust's <c>str::split_whitespace().next()</c>.
    /// </summary>
    private static string? FirstWhitespaceToken(string text)
    {
        string[] tokens = text.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
        return tokens.Length == 0 ? null : tokens[0];
    }

    /// <summary>Split on any newline convention, mirroring Rust's <c>str::lines</c>.</summary>
    private static IEnumerable<string> SplitLines(string text) =>
        text.Replace("\r\n", "\n").Replace('\r', '\n').Split('\n');
}
