namespace Stencil.TelegramBot.Domain.Llm;

// A validated op-plan (llm-contract.md §1). A variant branches from the state AFTER Actions and
// yields one extra output image.
public sealed record OpPlan(
    string Reply,
    IReadOnlyList<PlanAction> Actions,
    IReadOnlyList<OpVariant> Variants,
    AskCard? Ask = null);

// Max 16.
public sealed record OpVariant(string Label, IReadOnlyList<PlanAction> Actions);
