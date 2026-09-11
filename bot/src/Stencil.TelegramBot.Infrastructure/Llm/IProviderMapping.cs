using System.Text.Json;
using System.Text.Json.Nodes;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Infrastructure.Llm;

// One provider's §6 wire mapping. The canonical request/reply never change — only the wire
// shape does — so adding a provider is a table entry plus a file, never a client edit (the same
// shape as server/internal/llm's providerMapping).
internal interface IProviderMapping
{
    string Url(LlmChatRequest request, LlmOptions options);

    // Null for none.
    string? Bearer(LlmChatRequest request, LlmOptions options);

    JsonObject Body(LlmChatRequest request, LlmOptions options);

    // From a 2xx body; throws for the contract's truncation/refusal stop reasons and for a body
    // with no usable text.
    LlmReply Read(JsonElement root);
}
