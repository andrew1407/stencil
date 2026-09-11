using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Application.Llm;

/// <summary>What one op applier may touch besides the service: the user, the §1 frame mapper,
/// the turn's render/export buffers and the per-action notes.</summary>
public sealed record ActionContext(
    long UserId,
    List<PromptRender> Renders,
    List<PromptExport> Exports,
    PlanFrameMapper Mapper,
    List<string> Warnings);

/// <summary>One registry entry's applier — the Command half of a §13 entry. Two ops that share
/// a bullet share one handler and tell themselves apart by the concrete action.</summary>
public delegate Task OpHandler(PromptService service, PlanAction action, ActionContext ctx, CancellationToken ct);
