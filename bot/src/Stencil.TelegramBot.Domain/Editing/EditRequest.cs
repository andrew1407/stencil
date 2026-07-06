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

    // ── Collaboration server (server/) flags ──
    // Mirror the CLI's --server / --remote-update / --remote / --remote-name (see
    // cli/CONTRACT.md §1). Additive and default-off: existing callers that leave these unset
    // build the same argv as before. Kept in the port so the bot's CLI-contract adapter stays
    // conformant with mcp/src/args.rs even though the bot usually drives the server over REST.

    /// <summary>
    /// Connect to a collaboration server at this <c>http(s)://</c> URL; <see cref="Input"/> is
    /// then the <b>name of a project</b> to fetch and edit. Requires <see cref="Input"/>;
    /// incompatible with <see cref="Blank"/>.
    /// </summary>
    public string? Server { get; init; }

    /// <summary>With <see cref="Server"/>, write the result back into the fetched project.</summary>
    public bool RemoteUpdate { get; init; }

    /// <summary>Upload the result as a <b>new</b> project on the server at this URL.</summary>
    public string? Remote { get; init; }

    /// <summary>Name for the <see cref="Remote"/> project (default: input image base name).</summary>
    public string? RemoteName { get; init; }
}
