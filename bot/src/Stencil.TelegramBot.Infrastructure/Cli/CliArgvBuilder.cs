using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Exceptions;

namespace Stencil.TelegramBot.Infrastructure.Cli;

/// <summary>
/// Maps an <see cref="EditRequest"/> to the exact <c>stencil [options] &lt;output&gt;</c>
/// argv. A faithful port of <c>mcp/src/args.rs</c> (<c>build_argv</c>), with the same
/// validation invariants the CLI would otherwise reject with a terse message.
/// </summary>
/// <remarks>
/// The pipeline order is fixed by the CLI itself (source → crop → rotate → layout → filter →
/// encode), so argv order here is only cosmetic — the CLI parses flags order-independently.
/// </remarks>
public static class CliArgvBuilder
{
    /// <summary>
    /// Build the argv for one edit. Throws <see cref="StencilCliException"/> when the
    /// source/output/blank invariants are violated (exactly one of input/blank; non-empty
    /// output; blank width and height together or both omitted; a blank page format and
    /// explicit width/height are mutually exclusive).
    /// </summary>
    // ── CLI flag names ──
    // The exact option strings understood by the Zig CLI (cli/src/args.zig), centralized so the
    // flag contract is single-sourced and greppable — the .NET peer of mcp's FLAG_* consts
    // (mcp/src/args.rs). See cli/CONTRACT.md §1.
    private const string FlagServer = "--server";
    private const string FlagInput = "-i";
    private const string FlagBlank = "--blank";
    private const string FlagFrame = "-f";
    private const string FlagCrop = "-c";
    private const string FlagAlbum = "--album";
    private const string FlagRotate = "-r";
    private const string FlagLayout = "-l";
    private const string FlagFilter = "--filter";
    private const string FlagRemoteUpdate = "--remote-update";
    private const string FlagRemote = "--remote";
    private const string FlagRemoteName = "--remote-name";

    // ── Source-site scrape flags (DESIGN source-site contract §1) ──
    // The scrape mode's option strings, kept next to the edit flags so the whole CLI flag
    // contract is single-sourced here — the .NET peer of mcp's FLAG_SOURCE_* consts.
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

    public static IReadOnlyList<string> BuildArgv(EditRequest req)
    {
        bool hasInput = req.Input is not null;
        bool hasBlank = req.Blank is not null;
        if (hasInput && hasBlank)
        {
            throw new StencilCliException("`input` and `blank` are mutually exclusive — pass only one");
        }
        if (!hasInput && !hasBlank)
        {
            throw new StencilCliException(
                "no source — pass `input` (a path/URL), `blank`, or `server` + `input`");
        }
        if (string.IsNullOrWhiteSpace(req.Output))
        {
            throw new StencilCliException("`output` must not be empty");
        }
        // Flag-injection guard, mirroring build_argv in mcp/src/args.rs. The output is a
        // positional operand appended last, and the CLI (cli/src/args.zig) has no `--`
        // end-of-options terminator, so an output like `--album` or `-l` would be parsed as a
        // flag rather than the output path. A real output path never starts with a dash — the
        // CLI could never accept one in the positional slot — so reject one up front. (Today
        // Output is always a GUID workspace path, never user-supplied; this keeps the port in
        // sync and is defense-in-depth should that ever change.)
        if (req.Output.StartsWith('-'))
        {
            throw new StencilCliException(
                $"`output` must not start with '-' (got \"{req.Output}\") — a dash-leading value " +
                "would be parsed as a CLI flag, not the output path");
        }

        // Collaboration-server invariants, mirroring the CLI's own checks (cli/src/pipeline.zig)
        // and mcp/src/args.rs (Source::try_from).
        if (req.Server is not null)
        {
            if (hasBlank)
            {
                throw new StencilCliException(
                    "`server` fetches a project as the source — it can't be combined with `blank`");
            }
            if (!hasInput)
            {
                throw new StencilCliException(
                    "`server` needs `input` set to the name of the project to fetch");
            }
        }
        if (req.RemoteUpdate && req.Server is null)
        {
            throw new StencilCliException(
                "`remote_update` writes back to a fetched project — it needs `server` (and `input`)");
        }
        if (req.RemoteName is not null && req.Remote is null)
        {
            throw new StencilCliException(
                "`remote_name` names a `remote` upload — set `remote` (a server URL) too");
        }

        List<string> argv = new();

        // Source: `--server <url> -i <name>`, `-i <input>`, or the `--blank …` series.
        // `--server` conceptually precedes `-i` (it changes what `-i` means), though the CLI
        // parses order-independently.
        if (req.Server is not null)
        {
            argv.Add(FlagServer);
            argv.Add(req.Server);
        }

        if (req.Input is not null)
        {
            argv.Add(FlagInput);
            argv.Add(req.Input);
        }

        if (req.Blank is BlankSpec blank)
        {
            argv.Add(FlagBlank);
            bool hasWidth = blank.Width is not null;
            bool hasHeight = blank.Height is not null;
            if (blank.Page is not null && (hasWidth || hasHeight))
            {
                throw new StencilCliException(
                    "`blank.page` and `blank.width`/`blank.height` are mutually exclusive — " +
                    "name a page format or give pixel dims, not both");
            }
            if (blank.Page is not null)
            {
                argv.Add(blank.Page);
            }
            else if (hasWidth && hasHeight)
            {
                argv.Add(blank.Width!.Value.ToString());
                argv.Add(blank.Height!.Value.ToString());
            }
            else if (hasWidth || hasHeight)
            {
                throw new StencilCliException(
                    "`blank.width` and `blank.height` must be given together (or omit both for A4)");
            }
            if (blank.Color is not null)
            {
                argv.Add(blank.Color);
            }
        }

        if (req.Frame is int frame)
        {
            argv.Add(FlagFrame);
            argv.Add(frame.ToString());
        }

        if (req.CropSpec is not null)
        {
            string spec = req.CropSpec.Trim();
            if (spec.Length != 0)
            {
                argv.Add(FlagCrop);
                argv.Add(spec);
            }
        }

        if (req.Album)
        {
            argv.Add(FlagAlbum);
        }

        if (req.Rotate is int rotate)
        {
            argv.Add(FlagRotate);
            argv.Add(rotate.ToString());
        }

        if (req.LayoutPath is not null)
        {
            argv.Add(FlagLayout);
            argv.Add(req.LayoutPath);
        }

        if (req.Filter is not null)
        {
            argv.Add(FlagFilter);
            argv.Add(req.Filter);
        }

        // Server delivery: write the result back into the fetched project, and/or push it as a
        // new project. The result is always saved locally too (the positional output below).
        if (req.RemoteUpdate)
        {
            argv.Add(FlagRemoteUpdate);
        }

        if (req.Remote is not null)
        {
            argv.Add(FlagRemote);
            argv.Add(req.Remote);
        }

        if (req.RemoteName is not null)
        {
            argv.Add(FlagRemoteName);
            argv.Add(req.RemoteName);
        }

        argv.Add(req.Output);
        return argv;
    }

    /// <summary>
    /// Build the argv for one source-site scrape: <c>--source-site &lt;url&gt; [filters]
    /// &lt;output-dir&gt;</c>. Emits only the flags the request actually sets — an absent count/group
    /// or an unset (null or non-positive) dimension bound is left off, matching the CLI's own
    /// "0 = unset" / "count absent = all" semantics (DESIGN source-site contract §1). Throws
    /// <see cref="StencilCliException"/> when the url or output dir is empty, or when the output
    /// dir would be parsed as a flag (a dash-leading value), mirroring <see cref="BuildArgv"/>'s
    /// flag-injection guard on the positional operand.
    /// </summary>
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

        argv.Add(req.OutputDir);
        return argv;
    }

    /// <summary>Append <c>flag &lt;value&gt;</c> only for a set, positive dimension bound (0/null = unset).</summary>
    private static void AddBound(List<string> argv, string flag, int? value)
    {
        if (value is int px && px > 0)
        {
            argv.Add(flag);
            argv.Add(px.ToString());
        }
    }
}
