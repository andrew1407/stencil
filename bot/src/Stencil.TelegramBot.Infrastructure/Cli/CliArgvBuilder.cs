using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Exceptions;

namespace Stencil.TelegramBot.Infrastructure.Cli;

// Maps an EditRequest to the exact `stencil [options] <output>` argv — a port of mcp's
// build_argv, with the same validation invariants the CLI would otherwise reject tersely or,
// worse, silently skip. The pipeline order is the CLI's own, so argv order here is cosmetic.
public static partial class CliArgvBuilder
{
    // ── CLI flag names ──
    // The exact option strings the Zig CLI understands (cli/src/args.zig, cli/CONTRACT.md §1) —
    // the .NET peer of mcp's FLAG_* consts.
    private const string FlagServer = "--server";
    private const string FlagInput = "-i";
    private const string FlagBlank = "--blank";
    private const string FlagFrame = "-f";
    private const string FlagCrop = "-c";
    private const string FlagAlbum = "--album";
    private const string FlagRotate = "-r";
    private const string FlagLayout = "-l";
    private const string FlagFilter = "--filter";
    private const string FlagConfineOutput = "--confine-output";
    private const string FlagRemoteUpdate = "--remote-update";
    private const string FlagRemote = "--remote";
    private const string FlagRemoteName = "--remote-name";
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
        // Flag-injection guard, mirroring build_argv in mcp/src/args.rs: the output is a
        // positional operand and the CLI has no `--` end-of-options terminator, so an output like
        // `--album` would be parsed as a flag. A real output path never starts with a dash.
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

        // Source: `--server <url> -i <name>`, `-i <input>`, or the `--blank …` series. --server
        // conceptually precedes -i (it changes what -i means); the CLI parses order-independently.
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
                // The CLI SKIPS a colour it can't parse — the blank would come out white with no
                // error — so reject it here, exactly as mcp/src/args.rs does.
                if (!ColorSpec.IsValid(blank.Color))
                {
                    throw new StencilCliException(
                        $"`blank.color` isn't a colour the CLI understands (got \"{blank.Color}\") — " +
                        "use #rgb/#rrggbb/#rrggbbaa, a CSS colour name, or transparent");
                }
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

        // The adapter forwards paths it did not author, so the CLI refuses anything outside its
        // working directory — ProcessStencilCli picks that directory and passes the leaf here.
        argv.Add(FlagConfineOutput);
        argv.Add(req.Output);
        return argv;
    }
}
