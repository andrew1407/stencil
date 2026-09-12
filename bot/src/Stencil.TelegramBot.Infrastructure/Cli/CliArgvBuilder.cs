using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Exceptions;

namespace Stencil.TelegramBot.Infrastructure.Cli;

// A port of mcp's build_argv, with the same validation invariants; argv order is cosmetic, the
// pipeline order is the CLI's own.
public static partial class CliArgvBuilder
{
    // The exact option strings of cli/src/args.zig (cli/CONTRACT.md §1) — the peer of mcp's FLAG_*
    // consts.
    private const string _flagServer = "--server";
    private const string _flagInput = "-i";
    private const string _flagBlank = "--blank";
    private const string _flagFrame = "-f";
    private const string _flagCrop = "-c";
    private const string _flagAlbum = "--album";
    private const string _flagRotate = "-r";
    private const string _flagLayout = "-l";
    private const string _flagFilter = "--filter";
    private const string _flagConfineOutput = "--confine-output";
    private const string _flagRemoteUpdate = "--remote-update";
    private const string _flagRemote = "--remote";
    private const string _flagRemoteName = "--remote-name";
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
            argv.Add(_flagServer);
            argv.Add(req.Server);
        }

        if (req.Input is not null)
        {
            argv.Add(_flagInput);
            argv.Add(req.Input);
        }

        if (req.Blank is BlankSpec blank)
        {
            argv.Add(_flagBlank);
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
            argv.Add(_flagFrame);
            argv.Add(frame.ToString());
        }

        if (req.CropSpec is not null)
        {
            string spec = req.CropSpec.Trim();
            if (spec.Length != 0)
            {
                argv.Add(_flagCrop);
                argv.Add(spec);
            }
        }

        if (req.Album)
        {
            argv.Add(_flagAlbum);
        }

        if (req.Rotate is int rotate)
        {
            argv.Add(_flagRotate);
            argv.Add(rotate.ToString());
        }

        if (req.LayoutPath is not null)
        {
            argv.Add(_flagLayout);
            argv.Add(req.LayoutPath);
        }

        if (req.Filter is not null)
        {
            argv.Add(_flagFilter);
            argv.Add(req.Filter);
        }

        // The result is always saved locally too (the positional output below).
        if (req.RemoteUpdate)
        {
            argv.Add(_flagRemoteUpdate);
        }

        if (req.Remote is not null)
        {
            argv.Add(_flagRemote);
            argv.Add(req.Remote);
        }

        if (req.RemoteName is not null)
        {
            argv.Add(_flagRemoteName);
            argv.Add(req.RemoteName);
        }

        // The adapter forwards paths it did not author; ProcessStencilCli picks the working
        // directory and passes the leaf.
        argv.Add(_flagConfineOutput);
        argv.Add(req.Output);
        return argv;
    }
}
