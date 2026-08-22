namespace Stencil.TelegramBot.Domain.Llm;

/// <summary>
/// One conversation message: a role (<see cref="RoleUser"/>/<see cref="RoleAssistant"/>), its
/// text, and any attached images. History replay strips images from all but the most recent
/// image-bearing prior message (the contract's image-replay rule, §7).
/// </summary>
public sealed record LlmMessage(string Role, string Text, IReadOnlyList<LlmImage>? Images = null)
{
    public const string RoleUser = "user";
    public const string RoleAssistant = "assistant";

    public IReadOnlyList<LlmImage> Images { get; init; } = Images ?? [];
}
