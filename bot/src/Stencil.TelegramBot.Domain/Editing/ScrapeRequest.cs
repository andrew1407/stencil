namespace Stencil.TelegramBot.Domain.Editing;

// The `stencil --source-site <url> [filters] <output-dir>` argv as data (cli/CONTRACT.md §1).
// The CLI is the fetcher/HTML parser; core/ is untouched. Every min/max bound below is an
// inclusive pixel size, and null or non-positive means unset.
public sealed record ScrapeRequest
{
    // The http(s) page to scan; activates scrape mode.
    public required string Url { get; init; }

    // Items per group; null takes all matches and ignores Group.
    public int? Count { get; init; }

    // 0-based: the window is filtered[Group*Count : Group*Count+Count].
    public int? Group { get; init; }

    // img|video|background|poster, |-joined; null/"all" = every category.
    public string? Filter { get; init; }

    // Normalized extensions, |-joined; null/"all" = every format.
    public string? Format { get; init; }

    // --source-name: POSIX ERE, case-insensitive (substring on a Windows CLI build); null = every URL.
    public string? Name { get; init; }

    public int? MinWidth { get; init; }

    public int? MaxWidth { get; init; }

    public int? MinHeight { get; init; }

    public int? MaxHeight { get; init; }

    // Created if missing; the Application layer fills in a per-user scratch path.
    public string OutputDir { get; init; } = "";
}
