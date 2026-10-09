using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Domain.Abstractions;

public interface IStencilCli
{
    // source -> crop -> flip -> rotate -> filter -> layout -> encode. Throws StencilCliException.
    Task<RenderResult> EditAsync(EditRequest request, CancellationToken ct = default);

    // Decodes and re-encodes once: the CLI has no read-only metadata mode.
    Task<ImageSize> ProbeAsync(string input, CancellationToken ct = default);

    // --source-site mode; throws StencilCliException when nothing matched or the fetch failed.
    Task<ScrapeResult> ScrapeAsync(ScrapeRequest request, CancellationToken ct = default);

    // --script-plan: a .stc lowered to op-plan JSON; `input` is the frame % lengths resolve against.
    Task<ScriptPlan> ScriptPlanAsync(string scriptPath, string? input = null, CancellationToken ct = default);

    // --plan-check: core's op-plan verdict on a model reply, judged under the bot's surface.
    Task<PlanCheck> PlanCheckAsync(string reply, CancellationToken ct = default);

    // --merge-lines; a local line keyed like one in `seen` is dropped first, so a peer's delete stays.
    Task<LineMerge> MergeLinesAsync(IReadOnlyList<LayoutLine> peer, IReadOnlyList<LayoutLine> local,
        IReadOnlyList<LayoutLine> seen, CancellationToken ct = default);
}
