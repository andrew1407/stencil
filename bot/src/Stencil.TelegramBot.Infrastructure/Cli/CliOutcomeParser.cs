using Stencil.TelegramBot.Domain.Editing;

namespace Stencil.TelegramBot.Infrastructure.Cli;

/// <summary>
/// Parse the CLI's human-readable stderr into structured results. A faithful port of
/// <c>mcp/src/outcome.rs</c> (the output contract in <c>cli/CONTRACT.md</c> §2), op-for-op, so the
/// shared fixtures (<c>cli/testdata/outcome_fixtures.json</c>) pass identically on both sides.
/// </summary>
/// <remarks>
/// The CLI writes everything to <b>stderr</b> (stdout stays empty; the result is a written file):
/// on success one <c>wrote {path} ({w}x{h} px · {page})</c> line — or <c>wrote {path} (project)</c>
/// for a <c>.stencil</c> bundle — on failure one or more <c>error: …</c> lines, plus the
/// server-delivery lines. The child runs with <c>NO_COLOR=1</c>, so the text carries no ANSI.
/// </remarks>
public static partial class CliOutcomeParser
{
    // ── CLI output line prefixes ──
    // The exact stderr prefixes the CLI emits — the .NET peer of mcp's PREFIX_* consts.
    private const string PrefixWrote = "wrote ";
    private const string PrefixUpdated = "updated server result for project ";
    private const string PrefixCreated = "created server project ";
    private const string PrefixError = "error:";
    private const string SuffixProject = " (project)";
    private const string PrefixScraped = "scraped ";
    private const string IntoToken = " into ";

    /// <summary>
    /// The <c>wrote {path} ({w}x{h} …)</c> line, or null. Reverse-searches <c>" ("</c> so a path
    /// containing it still parses; mirrors <c>parse_wrote</c> in <c>mcp/src/outcome.rs</c>.
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
            // The dims lead; newer builds append " px · {page}" metadata (cm uses '×', not 'x').
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
    /// The <c>wrote {path} (project)</c> line a <c>.stencil</c> bundle write prints (contract §2.1
    /// <c>save</c>), or null. A project is a document, so the CLI reports no dimensions — which is
    /// why <see cref="ParseWrote"/> skips it. Mirrors <c>parse_wrote_project</c> in mcp.
    /// </summary>
    public static string? ParseWroteProject(string stderr)
    {
        foreach (string rawLine in SplitLines(stderr))
        {
            string line = rawLine.Trim();
            if (line.StartsWith(PrefixWrote, StringComparison.Ordinal)
                && line.EndsWith(SuffixProject, StringComparison.Ordinal))
            {
                return line[PrefixWrote.Length..^SuffixProject.Length];
            }
        }
        return null;
    }

    /// <summary>
    /// Every collaboration-server delivery line the CLI prints after a write, in order (one call
    /// can both update and create) — mirroring <c>parse_remotes</c> in <c>mcp/src/outcome.rs</c>.
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
    /// The <c>error: …</c> line(s), else the whole trimmed stderr, else a generic message —
    /// mirroring <c>extract_errors</c> in <c>mcp/src/outcome.rs</c>.
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

    /// <summary>Parse <c>{w}x{h}</c>, splitting on the first ASCII <c>'x'</c> (cm uses '×').</summary>
    private static bool TryParseWxH(string dims, out int width, out int height)
    {
        width = height = 0;
        int x = dims.IndexOf('x');
        return x >= 0
            && int.TryParse(dims[..x].Trim(), out width)
            && int.TryParse(dims[(x + 1)..].Trim(), out height);
    }

    /// <summary>The first whitespace-delimited token, or null — Rust's <c>split_whitespace().next()</c>.</summary>
    private static string? FirstWhitespaceToken(string text)
    {
        string[] tokens = text.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
        return tokens.Length == 0 ? null : tokens[0];
    }

    /// <summary>Split on any newline convention, mirroring Rust's <c>str::lines</c>.</summary>
    private static IEnumerable<string> SplitLines(string text) =>
        text.Replace("\r\n", "\n").Replace('\r', '\n').Split('\n');
}
