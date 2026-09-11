namespace Stencil.TelegramBot.Domain.Llm;

// History replay strips images from all but the most recent image-bearing prior message (the
// §7 image-replay rule).
public sealed record LlmMessage(string Role, string Text, IReadOnlyList<LlmImage>? Images = null)
{
    public const string RoleUser = "user";
    public const string RoleAssistant = "assistant";

    public IReadOnlyList<LlmImage> Images { get; init; } = Images ?? [];
}
