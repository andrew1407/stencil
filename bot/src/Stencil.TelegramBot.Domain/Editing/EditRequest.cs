namespace Stencil.TelegramBot.Domain.Editing;

// One `stencil [options] <output>` command line as data; the adapter maps it to argv, mirroring
// mcp/src/args.rs build_argv. Exactly one source: Input (path or http(s) URL) or Blank.
public sealed record EditRequest
{
    public string? Input { get; init; }
    public BlankSpec? Blank { get; init; }
    public int? Frame { get; init; }
    public string? CropSpec { get; init; }
    public bool Album { get; init; }
    public int? Rotate { get; init; }

    // Path or URL for --layout; the caller materialises inline layouts.
    public string? LayoutPath { get; init; }
    public string? Filter { get; init; }

    // A missing/unknown extension is auto-filled by the CLI.
    public required string Output { get; init; }

    // When false the adapter refuses to overwrite an existing Output.
    public bool Overwrite { get; init; }

    // ── Collaboration server flags (cli/CONTRACT.md §1) ──
    // Default-off, and kept even though the bot drives the server over REST: the CLI-contract
    // adapter has to stay argv-conformant with mcp/src/args.rs.

    // With Server, Input is the NAME of a project to fetch and edit; incompatible with Blank.
    public string? Server { get; init; }

    public bool RemoteUpdate { get; init; }

    // Upload the result as a NEW project on the server at this URL.
    public string? Remote { get; init; }

    // Default: the input image's base name.
    public string? RemoteName { get; init; }
}
