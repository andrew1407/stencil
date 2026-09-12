namespace Stencil.TelegramBot.Domain.Llm;

// History replay keeps images only on the most recent image-bearing prior message (§7).
public sealed record LlmMessage(string Role, string Text, IReadOnlyList<LlmImage>? Images = null)
{
    public const string RoleUser = "user";
    public const string RoleAssistant = "assistant";

    public IReadOnlyList<LlmImage> Images { get; init; } = Images ?? [];
}
