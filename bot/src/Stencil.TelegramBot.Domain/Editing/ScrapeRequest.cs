namespace Stencil.TelegramBot.Domain.Editing;

/// <summary>
/// A single source-site scrape invocation: the page to scan plus the category/format/dimension
/// filters and the paging window, expressed as data. The Stencil CLI adapter maps this to the
/// <c>stencil --source-site &lt;url&gt; [filters] &lt;output-dir&gt;</c> argv, mirroring the flag
/// contract in <c>cli/CONTRACT.md</c> §1 (and the DESIGN source-site contract §1) that the
/// other surfaces (CLI, pystencil, MCP) share.
/// </summary>
/// <remarks>
/// The CLI is the fetcher/HTML parser (core/ is untouched); this record only carries the flags.
/// <see cref="Count"/> absent ⇒ every match (the group is then ignored); <see cref="Group"/> is a
/// 0-based page index over the filtered list. The min/max width/height bounds are inclusive pixel
/// sizes; a null (or non-positive) bound is unset. <see cref="OutputDir"/> is the destination
/// directory (created if missing); the Application layer fills it with a per-user scratch path.
/// </remarks>
public sealed record ScrapeRequest
{
    /// <summary>The http(s) page URL to scan; activates scrape mode.</summary>
    public required string Url { get; init; }

    /// <summary>Items per page/group; null ⇒ take all matches (group ignored).</summary>
    public int? Count { get; init; }

    /// <summary>0-based page index; the window is <c>filtered[Group*Count : Group*Count+Count]</c>.</summary>
    public int? Group { get; init; }

    /// <summary>Category tokens (<c>img|video|background|poster</c>), <c>|</c>-joined; null/"all" = every category.</summary>
    public string? Filter { get; init; }

    /// <summary>Format tokens (normalized extensions), <c>|</c>-joined; null/"all" = every format.</summary>
    public string? Format { get; init; }

    /// <summary>
    /// Regex matched against each media URL (POSIX ERE / case-insensitive on the CLI; substring
    /// on a Windows CLI build). Null/empty = every URL. Passed through as <c>--source-name</c>.
    /// </summary>
    public string? Name { get; init; }

    /// <summary>Inclusive minimum width in px; null/0 = unset.</summary>
    public int? MinWidth { get; init; }

    /// <summary>Inclusive maximum width in px; null/0 = unset.</summary>
    public int? MaxWidth { get; init; }

    /// <summary>Inclusive minimum height in px; null/0 = unset.</summary>
    public int? MinHeight { get; init; }

    /// <summary>Inclusive maximum height in px; null/0 = unset.</summary>
    public int? MaxHeight { get; init; }

    /// <summary>Destination directory for the downloads (created if missing). Set by the service.</summary>
    public string OutputDir { get; init; } = "";
}
