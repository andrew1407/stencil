using System.Text.Json;
using System.Text.Json.Nodes;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Infrastructure.Llm;

// One provider's §6 wire mapping; adding a provider is a table entry plus a file
// (server/internal/llm's providerMapping shape).
internal interface IProviderMapping
{
    string Url(LlmChatRequest request, LlmOptions options);

    string? Bearer(LlmChatRequest request, LlmOptions options);

    JsonObject Body(LlmChatRequest request, LlmOptions options);

    // Throws for the contract's truncation/refusal stop reasons and for a body with no usable text.
    LlmReply Read(JsonElement root);
}
