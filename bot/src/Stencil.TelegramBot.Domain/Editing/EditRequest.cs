namespace Stencil.TelegramBot.Domain.Editing;

// One CLI command line as data; the adapter maps it to argv, mirroring mcp/src/args.rs build_argv.
public sealed record EditRequest
{
    public string? Input { get; init; }
    public BlankSpec? Blank { get; init; }
    public int? Frame { get; init; }
    public string? CropSpec { get; init; }
    public bool Album { get; init; }
    public int? Rotate { get; init; }

    public string? LayoutPath { get; init; }
    public string? Filter { get; init; }

    // A missing/unknown extension is auto-filled by the CLI.
    public required string Output { get; init; }

    // When false the adapter refuses to overwrite an existing Output.
    public bool Overwrite { get; init; }

    // Server flags (cli/CONTRACT.md §1), kept so the adapter stays argv-conformant with mcp. With
    // Server, Input is the NAME of a project to fetch and edit; incompatible with Blank.
    public string? Server { get; init; }

    public bool RemoteUpdate { get; init; }

    public string? Remote { get; init; }

    // Default: the input image's base name.
    public string? RemoteName { get; init; }
}
