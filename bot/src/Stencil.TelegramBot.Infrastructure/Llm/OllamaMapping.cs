using System.Text.Json;
using System.Text.Json.Nodes;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Serialization;

namespace Stencil.TelegramBot.Infrastructure.Llm;

/// <summary>§6.1 ollama native chat: <c>POST {baseUrl}/api/chat</c>, images as bare base64.</summary>
internal sealed class OllamaMapping : IProviderMapping
{
    public string Url(LlmChatRequest request, LlmOptions options) =>
        options.BaseUrl.TrimEnd('/') + "/api/chat";

    public string? Bearer(LlmChatRequest request, LlmOptions options) => null;

    public JsonObject Body(LlmChatRequest request, LlmOptions options)
    {
        JsonArray messages = new() { new JsonObject { ["role"] = "system", ["content"] = request.System } };
        foreach (LlmMessage message in request.Messages)
        {
            JsonObject entry = new() { ["role"] = message.Role, ["content"] = message.Text };
            if (message.Images.Count > 0)
            {
                JsonArray images = new();
                foreach (LlmImage image in message.Images)
                {
                    images.Add(image.Base64Data);
                }
                entry["images"] = images;
            }
            messages.Add(entry);
        }
        return new JsonObject
        {
            ["model"] = options.Model,
            ["stream"] = false,
            ["messages"] = messages,
        };
    }

    public LlmReply Read(JsonElement root)
    {
        // Ollama reports a truncated generation as done_reason "length".
        if (JsonRead.ReadString(root, "done_reason") == "length")
        {
            throw ProviderReply.Truncated();
        }
        return new LlmReply(ProviderReply.MessageContent(root)
            ?? throw new LlmException("malformed ollama response (no message.content)"));
    }
}
