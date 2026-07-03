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
            throw new StencilCliException("no source — pass `input` (a path/URL) or `blank`");
        }
        if (string.IsNullOrWhiteSpace(req.Output))
        {
            throw new StencilCliException("`output` must not be empty");
        }

        List<string> argv = new();

        if (req.Input is not null)
        {
            argv.Add("-i");
            argv.Add(req.Input);
        }

        if (req.Blank is BlankSpec blank)
        {
            argv.Add("--blank");
            bool hasWidth = blank.Width is not null;
            bool hasHeight = blank.Height is not null;
            if (blank.Page is not null && (hasWidth || hasHeight))
            {
                throw new StencilCliException(
                    "`blank.page` and `blank.width`/`blank.height` are mutually exclusive — the format names the size");
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
            argv.Add("-f");
            argv.Add(frame.ToString());
        }

        if (req.CropSpec is not null)
        {
            string spec = req.CropSpec.Trim();
            if (spec.Length != 0)
            {
                argv.Add("-c");
                argv.Add(spec);
            }
        }

        if (req.Album)
        {
            argv.Add("--album");
        }

        if (req.Rotate is int rotate)
        {
            argv.Add("-r");
            argv.Add(rotate.ToString());
        }

        if (req.LayoutPath is not null)
        {
            argv.Add("-l");
            argv.Add(req.LayoutPath);
        }

        if (req.Filter is not null)
        {
            argv.Add("--filter");
            argv.Add(req.Filter);
        }

        argv.Add(req.Output);
        return argv;
    }
}
