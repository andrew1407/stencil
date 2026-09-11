using System.Text.Json;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Infrastructure.Llm;

/// <summary>The reply-reading bits more than one §6 mapping needs.</summary>
internal static class ProviderReply
{
    /// <summary>The text at <c>{parent}.message.content</c>, or null when absent / not a
    /// string — a typed bad-reply seam, never a silent "".</summary>
    public static string? MessageContent(JsonElement parent) =>
        parent.TryGetProperty("message", out JsonElement message) && message.ValueKind == JsonValueKind.Object
            && message.TryGetProperty("content", out JsonElement content)
            && content.ValueKind == JsonValueKind.String
            ? content.GetString() ?? ""
            : null;

    public static LlmException Truncated() => new(
        "The AI response was cut off at the token limit — try a shorter or simpler request.",
        LlmFailure.Truncated);
}
