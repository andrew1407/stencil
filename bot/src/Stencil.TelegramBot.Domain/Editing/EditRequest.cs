namespace Stencil.TelegramBot.Domain.Editing;

/// <summary>
/// A single low-level CLI invocation: the exact <c>stencil [options] &lt;output&gt;</c>
/// command line, expressed as data. The Stencil CLI adapter maps this to argv, mirroring
/// <c>mcp/src/args.rs</c> (<c>build_argv</c>) and <c>cli/src/args.zig</c>.
/// </summary>
/// <remarks>
/// Exactly one source must be set: <see cref="Input"/> (a local path or <c>http(s)://</c>
/// URL) or <see cref="Blank"/>. The pipeline order is fixed by the CLI itself, so the field
/// order here is irrelevant — the CLI parses flags order-independently.
/// </remarks>
public sealed record EditRequest
{
    public string? Input { get; init; }
    public BlankSpec? Blank { get; init; }
    public int? Frame { get; init; }
    public string? CropSpec { get; init; }
    public bool Album { get; init; }
    public int? Rotate { get; init; }

    /// <summary>Path (or URL) passed to <c>--layout</c>; the caller materialises inline layouts.</summary>
    public string? LayoutPath { get; init; }
    public string? Filter { get; init; }

    /// <summary>Result file path. A missing/unknown extension is auto-filled by the CLI.</summary>
    public required string Output { get; init; }

    /// <summary>When false the adapter refuses to overwrite an existing <see cref="Output"/>.</summary>
    public bool Overwrite { get; init; }
}
