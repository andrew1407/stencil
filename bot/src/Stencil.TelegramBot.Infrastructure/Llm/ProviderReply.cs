using System.Text.Json;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Llm.Wire;

namespace Stencil.TelegramBot.Infrastructure.Llm;

internal static class ProviderReply
{
    // Null when absent / not a string — a typed bad-reply seam, never a silent "".
    public static string? MessageContent(JsonElement parent) =>
        parent.TryGetProperty("message", out JsonElement message) && message.ValueKind == JsonValueKind.Object
            && message.TryGetProperty("content", out JsonElement content)
            && content.ValueKind == JsonValueKind.String
            ? content.GetString() ?? ""
            : null;

    public static LlmException Truncated() => new(
        "The AI response was cut off at the token limit — try a shorter or simpler request.",
        LlmFailure.TRUNCATED);
}
