using Stencil.TelegramBot.Domain.Editing;

namespace Stencil.TelegramBot.Infrastructure.Cli;

// CliOutcomeParser — the `--source-site` scrape run's stderr. Class doc lives in
// CliOutcomeParser.cs.
public static partial class CliOutcomeParser
{
    /// <summary>
    /// The multi-file stderr of a source-site scrape (DESIGN source-site contract §3): each
    /// <c>wrote …</c> line is one downloaded file (dims null when the leading token isn't WxH — a
    /// video/unmeasured line), and the trailing <c>scraped … into {dir}</c> line gives the
    /// destination. Pinned by <c>cli/testdata/scrape_fixtures.json</c>, op-for-op with mcp's
    /// <c>parse_scraped</c>.
    /// </summary>
    public static ScrapeResult ParseScraped(string stderr)
    {
        List<ScrapedFile> files = new();
        string directory = "";
        foreach (string rawLine in SplitLines(stderr))
        {
            string line = rawLine.Trim();
            if (line.StartsWith(PrefixWrote, StringComparison.Ordinal))
            {
                string rest = line[PrefixWrote.Length..];
                // rfind " (" so a path containing " (" (e.g. "img (1)") still parses.
                int open = rest.LastIndexOf(" (", StringComparison.Ordinal);
                if (open < 0)
                {
                    files.Add(new ScrapedFile(rest, null, null));
                    continue;
                }
                string path = rest[..open];
                string tail = rest[(open + 2)..];
                if (tail.EndsWith(')'))
                {
                    tail = tail[..^1];
                }
                // Only a leading WxH token is dims; a video line leads with "source …" → null.
                string? token = FirstWhitespaceToken(tail);
                if (token is not null && TryParseWxH(token, out int width, out int height))
                {
                    files.Add(new ScrapedFile(path, width, height));
                }
                else
                {
                    files.Add(new ScrapedFile(path, null, null));
                }
            }
            else if (line.StartsWith(PrefixScraped, StringComparison.Ordinal))
            {
                // `scraped {n} file(s) from {host} into {dir}` — rfind, a path may contain " into ".
                int into = line.LastIndexOf(IntoToken, StringComparison.Ordinal);
                if (into >= 0)
                {
                    directory = line[(into + IntoToken.Length)..].Trim();
                }
            }
        }
        return new ScrapeResult(directory, files);
    }
}
