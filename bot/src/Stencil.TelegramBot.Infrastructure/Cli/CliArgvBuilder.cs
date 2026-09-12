using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Exceptions;

namespace Stencil.TelegramBot.Infrastructure.Cli;

// A port of mcp's build_argv, with the same validation invariants; argv order is cosmetic, the
// pipeline order is the CLI's own.
public static partial class CliArgvBuilder
{
    // The exact option strings of cli/src/args.zig (cli/CONTRACT.md §1) — the peer of mcp's FLAG_*
    // consts.
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
        // Flag-injection guard (mcp build_argv): no `--` terminator, so an output like `--album`
        // would parse as a flag.
        if (req.Output.StartsWith('-'))
        {
            throw new StencilCliException(
                $"`output` must not start with '-' (got \"{req.Output}\") — a dash-leading value " +
                "would be parsed as a CLI flag, not the output path");
        }

        // Server invariants, mirroring cli/src/pipeline.zig and mcp's Source::try_from.
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

        // --server changes what -i means; the CLI parses order-independently.
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
                // The CLI SKIPS a colour it can't parse (the blank would come out white), so reject
                // it here like mcp does.
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

        // The result is always saved locally too (the positional output below).
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

        // The adapter forwards paths it did not author; ProcessStencilCli picks the working
        // directory and passes the leaf.
        argv.Add(FlagConfineOutput);
        argv.Add(req.Output);
        return argv;
    }
}
