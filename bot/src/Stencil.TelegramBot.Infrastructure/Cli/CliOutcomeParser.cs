using Stencil.TelegramBot.Domain.Editing;

namespace Stencil.TelegramBot.Infrastructure.Cli;

/// <summary>
/// Parse the CLI's human-readable stderr into structured results. A faithful port of
/// <c>mcp/src/outcome.rs</c>.
/// </summary>
/// <remarks>
/// The CLI writes everything — banner, usage, errors, and the success line — to <b>stderr</b>
/// (stdout stays empty; the result is a written file). On success it prints exactly one line
/// whose dimensions lead the parenthesised tail, e.g.
/// <c>wrote out.png (16x12 px · A4 29.7×21cm)</c> (the simple <c>(16x12)</c> form is also
/// accepted); on failure one or more <c>error: …</c> lines. The child runs with
/// <c>NO_COLOR=1</c> so this text is free of ANSI escapes.
/// </remarks>
public static class CliOutcomeParser
{
    /// <summary>
    /// Find and parse the <c>wrote {path} ({w}x{h})</c> line, or null when absent. Uses a
    /// reverse search for <c>" ("</c> so paths containing <c>" ("</c> still parse.
    /// </summary>
    public static RenderResult? ParseWrote(string stderr)
    {
        foreach (string rawLine in SplitLines(stderr))
        {
            string line = rawLine.Trim();
            if (!line.StartsWith("wrote ", StringComparison.Ordinal))
            {
                continue;
            }
            string rest = line["wrote ".Length..];
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
            // The tail leads with "{w}x{h}" and may carry extra text (" px · A4 …"); read the
            // leading integer on each side of the first ASCII 'x'. The cm size uses '×' (U+00D7),
            // not 'x', so the first 'x' is always the pixel-dimension separator.
            string dims = tail[..^1];
            int x = dims.IndexOf('x');
            if (x < 0)
            {
                continue;
            }
            if (TryLeadingInt(TrimToDigitsTail(dims[..x]), out int width)
                && TryLeadingInt(dims[(x + 1)..].TrimStart(), out int height))
            {
                return new RenderResult(path, width, height);
            }
        }
        return null;
    }

    /// <summary>Parse the run of digits at the start of <paramref name="text"/> (else fail).</summary>
    private static bool TryLeadingInt(string text, out int value)
    {
        int end = 0;
        while (end < text.Length && char.IsAsciiDigit(text[end]))
        {
            end++;
        }
        return int.TryParse(text[..end], out value);
    }

    /// <summary>Keep only the trailing run of digits (drops any leading label before the width).</summary>
    private static string TrimToDigitsTail(string text)
    {
        string trimmed = text.Trim();
        int start = trimmed.Length;
        while (start > 0 && char.IsAsciiDigit(trimmed[start - 1]))
        {
            start--;
        }
        return trimmed[start..];
    }

    /// <summary>
    /// Pull the <c>error: …</c> line(s) out of stderr for surfacing back to the caller. Falls
    /// back to the whole trimmed stderr when no <c>error:</c> prefix is found, and to a generic
    /// message when stderr is empty.
    /// </summary>
    public static string ExtractErrors(string stderr)
    {
        List<string> errors = new();
        foreach (string rawLine in SplitLines(stderr))
        {
            string line = rawLine.Trim();
            if (line.StartsWith("error:", StringComparison.Ordinal))
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

    /// <summary>Split on any newline convention, mirroring Rust's <c>str::lines</c>.</summary>
    private static IEnumerable<string> SplitLines(string text) =>
        text.Replace("\r\n", "\n").Replace('\r', '\n').Split('\n');
}
