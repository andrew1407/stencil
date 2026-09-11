using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Exceptions;

namespace Stencil.TelegramBot.Infrastructure.Cli;

// CliArgvBuilder — the source-site scrape argv and the flags only it uses. Class doc lives in
// CliArgvBuilder.cs.
public static partial class CliArgvBuilder
{
    // ── Source-site scrape flags (DESIGN source-site contract §1) ──
    // Kept next to the edit flags so the whole CLI flag contract is single-sourced here.
    private const string FlagSourceSite = "--source-site";
    private const string FlagSourceCount = "--source-count";
    private const string FlagSourceGroup = "--group";
    private const string FlagSourceFilter = "--source-filter";
    private const string FlagSourceFormat = "--source-format";
    private const string FlagSourceName = "--source-name";
    private const string FlagSourceMinWidth = "--source-min-width";
    private const string FlagSourceMaxWidth = "--source-max-width";
    private const string FlagSourceMinHeight = "--source-min-height";
    private const string FlagSourceMaxHeight = "--source-max-height";

    // `--source-site <url> [filters] <output-dir>`. Emits only the flags the request actually
    // sets, matching the CLI's "0 = unset" / "count absent = all" semantics (source-site §1).
    public static IReadOnlyList<string> BuildScrapeArgv(ScrapeRequest req)
    {
        if (string.IsNullOrWhiteSpace(req.Url))
        {
            throw new StencilCliException("`url` must not be empty — pass the page to scrape");
        }
        if (string.IsNullOrWhiteSpace(req.OutputDir))
        {
            throw new StencilCliException("`output` directory must not be empty");
        }
        // The output directory is the positional operand appended last, and the CLI has no `--`
        // end-of-options terminator, so a dash-leading value would be parsed as a flag. Reject
        // it up front, exactly like BuildArgv guards the edit output.
        if (req.OutputDir.StartsWith('-'))
        {
            throw new StencilCliException(
                $"`output` directory must not start with '-' (got \"{req.OutputDir}\") — a " +
                "dash-leading value would be parsed as a CLI flag, not the output path");
        }

        List<string> argv = new()
        {
            FlagSourceSite,
            req.Url,
        };

        if (req.Count is int count)
        {
            argv.Add(FlagSourceCount);
            argv.Add(count.ToString());
        }
        if (req.Group is int group)
        {
            argv.Add(FlagSourceGroup);
            argv.Add(group.ToString());
        }
        if (!string.IsNullOrWhiteSpace(req.Filter))
        {
            argv.Add(FlagSourceFilter);
            argv.Add(req.Filter);
        }
        if (!string.IsNullOrWhiteSpace(req.Format))
        {
            argv.Add(FlagSourceFormat);
            argv.Add(req.Format);
        }
        if (!string.IsNullOrWhiteSpace(req.Name))
        {
            argv.Add(FlagSourceName);
            argv.Add(req.Name);
        }
        AddBound(argv, FlagSourceMinWidth, req.MinWidth);
        AddBound(argv, FlagSourceMaxWidth, req.MaxWidth);
        AddBound(argv, FlagSourceMinHeight, req.MinHeight);
        AddBound(argv, FlagSourceMaxHeight, req.MaxHeight);

        argv.Add(FlagConfineOutput);
        argv.Add(req.OutputDir);
        return argv;
    }

    // Only for a set, positive bound: 0/null is unset.
    private static void AddBound(List<string> argv, string flag, int? value)
    {
        if (value is int px && px > 0)
        {
            argv.Add(flag);
            argv.Add(px.ToString());
        }
    }
}
