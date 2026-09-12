using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Exceptions;

namespace Stencil.TelegramBot.Infrastructure.Cli;

public static partial class CliArgvBuilder
{
    // Source-site scrape flags (source-site contract §1), single-sourced beside the edit flags.
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

    // Emits only the flags the request sets, matching the CLI's "0 = unset" / "count absent = all"
    // (§1).
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
        // The CLI has no `--` terminator, so a dash-leading positional would parse as a flag — like
        // BuildArgv.
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
        addBound(argv, FlagSourceMinWidth, req.MinWidth);
        addBound(argv, FlagSourceMaxWidth, req.MaxWidth);
        addBound(argv, FlagSourceMinHeight, req.MinHeight);
        addBound(argv, FlagSourceMaxHeight, req.MaxHeight);

        argv.Add(FlagConfineOutput);
        argv.Add(req.OutputDir);
        return argv;
    }

    // Only for a set, positive bound: 0/null is unset.
    private static void addBound(List<string> argv, string flag, int? value)
    {
        if (value is int px && px > 0)
        {
            argv.Add(flag);
            argv.Add(px.ToString());
        }
    }
}
