using Stencil.TelegramBot.Domain.Editing;

namespace Stencil.TelegramBot.Domain.Abstractions;

public interface IStencilCli
{
    // source -> crop -> rotate -> filter -> layout -> encode. Throws StencilCliException.
    Task<RenderResult> EditAsync(EditRequest request, CancellationToken ct = default);

    // Decodes and re-encodes once: the CLI has no read-only metadata mode.
    Task<ImageSize> ProbeAsync(string input, CancellationToken ct = default);

    // --source-site mode; throws StencilCliException when nothing matched or the fetch failed.
    Task<ScrapeResult> ScrapeAsync(ScrapeRequest request, CancellationToken ct = default);
}
