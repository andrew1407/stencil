namespace Stencil.TelegramBot.Domain.Editing;

// One CLI command line as data; the adapter maps it to argv, mirroring mcp/src/args/ build_argv.
public sealed record EditRequest
{
    public string? Input { get; init; }
    public BlankSpec? Blank { get; init; }
    public int? Frame { get; init; }
    public string? CropSpec { get; init; }
    public bool Album { get; init; }
    public int? Rotate { get; init; }
    public bool Flip { get; init; }

    public string? LayoutPath { get; init; }
    public string? Filter { get; init; }

    // A missing/unknown extension is auto-filled by the CLI.
    public required string Output { get; init; }

    public bool Overwrite { get; init; }
}
