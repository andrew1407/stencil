using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Application.Llm.Plan;

public sealed record ActionContext(
    long UserId,
    List<PromptRender> Renders,
    List<PromptExport> Exports,
    PlanFrameMapper Mapper,
    List<string> Warnings);

// The Command half of a §13 entry; two ops sharing a bullet share one handler.
public delegate Task OpHandler(PromptService service, PlanAction action, ActionContext ctx, CancellationToken ct);
