using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Exceptions;

namespace Stencil.TelegramBot.Infrastructure.Cli;

public static partial class CliArgvBuilder
{
    // Source-site scrape flags (source-site contract §1), single-sourced beside the edit flags.
    private const string _flagSourceSite = "--source-site";
    private const string _flagSourceCount = "--source-count";
    private const string _flagSourceGroup = "--group";
    private const string _flagSourceFilter = "--source-filter";
    private const string _flagSourceFormat = "--source-format";
    private const string _flagSourceName = "--source-name";
    private const string _flagSourceMinWidth = "--source-min-width";
    private const string _flagSourceMaxWidth = "--source-max-width";
    private const string _flagSourceMinHeight = "--source-min-height";
    private const string _flagSourceMaxHeight = "--source-max-height";

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
            _flagSourceSite,
            req.Url,
        };

        if (req.Count is int count)
        {
            argv.Add(_flagSourceCount);
            argv.Add(count.ToString());
        }
        if (req.Group is int group)
        {
            argv.Add(_flagSourceGroup);
            argv.Add(group.ToString());
        }
        if (!string.IsNullOrWhiteSpace(req.Filter))
        {
            argv.Add(_flagSourceFilter);
            argv.Add(req.Filter);
        }
        if (!string.IsNullOrWhiteSpace(req.Format))
        {
            argv.Add(_flagSourceFormat);
            argv.Add(req.Format);
        }
        if (!string.IsNullOrWhiteSpace(req.Name))
        {
            argv.Add(_flagSourceName);
            argv.Add(req.Name);
        }
        addBound(argv, _flagSourceMinWidth, req.MinWidth);
        addBound(argv, _flagSourceMaxWidth, req.MaxWidth);
        addBound(argv, _flagSourceMinHeight, req.MinHeight);
        addBound(argv, _flagSourceMaxHeight, req.MaxHeight);

        argv.Add(_flagConfineOutput);
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
