namespace Stencil.TelegramBot.Domain.Llm;

/// <summary>
/// A validated op-plan (<c>llm-contract.md</c> §1): the chat reply, the actions that
/// mutate the working image in order, and the variants that each branch from the state after
/// <see cref="Actions"/> and yield one extra output image.
/// </summary>
public sealed record OpPlan(
    string Reply,
    IReadOnlyList<PlanAction> Actions,
    IReadOnlyList<OpVariant> Variants,
    AskCard? Ask = null);

/// <summary>One plan variant: a short human label plus its own actions (max 16).</summary>
public sealed record OpVariant(string Label, IReadOnlyList<PlanAction> Actions);
