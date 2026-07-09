using Stencil.TelegramBot.Domain.Editing;

namespace Stencil.TelegramBot.Domain.Abstractions;

/// <summary>
/// The pixel engine: a thin port over the Zig CLI (<c>cli/</c>), which wraps the shared
/// C++ <c>core/</c>. Every transform runs through here, so results match the browser,
/// desktop, CLI and Python front-ends by construction. Conceptually the .NET sibling of
/// <c>mcp/src/pipeline.rs</c>.
/// </summary>
public interface IStencilCli
{
    /// <summary>
    /// Run one edit (source → crop → rotate → layout → filter → encode) and return the
    /// written file's path and dimensions. Throws <see cref="Exceptions.StencilCliException"/>
    /// on failure.
    /// </summary>
    Task<RenderResult> EditAsync(EditRequest request, CancellationToken ct = default);

    /// <summary>
    /// Read an image/video source's pixel dimensions (decode + re-encode once, since the CLI
    /// has no read-only metadata mode). <paramref name="input"/> is a path or http(s) URL.
    /// </summary>
    Task<ImageSize> ProbeAsync(string input, CancellationToken ct = default);

    /// <summary>
    /// Scrape a web page (<c>--source-site</c> mode): the CLI fetches the page, extracts and
    /// filters its media URLs, and downloads the matches into <see cref="ScrapeRequest.OutputDir"/>.
    /// Returns the written files (with measured dimensions where the CLI could sniff them). Throws
    /// <see cref="Exceptions.StencilCliException"/> when nothing matched or the fetch failed. The
    /// CLI is the HTML parser/fetcher — <c>core/</c> is not involved.
    /// </summary>
    Task<ScrapeResult> ScrapeAsync(ScrapeRequest request, CancellationToken ct = default);
}
