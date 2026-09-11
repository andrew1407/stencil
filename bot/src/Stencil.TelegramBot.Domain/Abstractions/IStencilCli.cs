using Stencil.TelegramBot.Domain.Editing;

namespace Stencil.TelegramBot.Domain.Abstractions;

// The pixel engine: a port over the Zig CLI, which wraps the shared C++ core. Every transform
// runs through here, so results match the other front-ends by construction.
public interface IStencilCli
{
    // source -> crop -> rotate -> filter -> layout -> encode. Throws StencilCliException.
    Task<RenderResult> EditAsync(EditRequest request, CancellationToken ct = default);

    // Decodes and re-encodes once: the CLI has no read-only metadata mode.
    Task<ImageSize> ProbeAsync(string input, CancellationToken ct = default);

    // --source-site mode; the CLI is the fetcher/HTML parser, core/ is not involved. Throws
    // StencilCliException when nothing matched or the fetch failed.
    Task<ScrapeResult> ScrapeAsync(ScrapeRequest request, CancellationToken ct = default);
}
