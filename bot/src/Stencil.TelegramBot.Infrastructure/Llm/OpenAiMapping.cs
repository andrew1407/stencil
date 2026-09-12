using System.Text.Json;
using System.Text.Json.Nodes;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Serialization;

namespace Stencil.TelegramBot.Infrastructure.Llm;

/// <summary>§6.2 openai-compat: <c>POST {baseUrl}/chat/completions</c>, optional bearer key,
/// images as <c>image_url</c> data-URI parts.</summary>
internal sealed class OpenAiMapping : IProviderMapping
{
    public string Url(LlmChatRequest request, LlmOptions options) =>
        options.BaseUrl.TrimEnd('/') + "/chat/completions";

    public string? Bearer(LlmChatRequest request, LlmOptions options) =>
        options.ApiKey.Length == 0 ? null : options.ApiKey;

    public JsonObject Body(LlmChatRequest request, LlmOptions options)
    {
        JsonArray messages = new() { new JsonObject { ["role"] = "system", ["content"] = request.System } };
        foreach (LlmMessage message in request.Messages)
        {
            messages.Add(new JsonObject { ["role"] = message.Role, ["content"] = content(message) });
        }
        return new JsonObject
        {
            ["model"] = options.Model,
            ["stream"] = false,
            ["messages"] = messages,
        };
    }

    private static JsonNode content(LlmMessage message)
    {
        if (message.Images.Count == 0)
        {
            return JsonValue.Create(message.Text);
        }
        JsonArray parts = new() { new JsonObject { ["type"] = "text", ["text"] = message.Text } };
        foreach (LlmImage image in message.Images)
        {
            parts.Add(new JsonObject
            {
                ["type"] = "image_url",
                ["image_url"] = new JsonObject { ["url"] = $"data:{image.MediaType};base64,{image.Base64Data}" },
            });
        }
        return parts;
    }

    public LlmReply Read(JsonElement root)
    {
        if (!root.TryGetProperty("choices", out JsonElement choices) || choices.ValueKind != JsonValueKind.Array
            || choices.GetArrayLength() == 0)
        {
            throw new LlmException("malformed response (no choices[0].message.content)");
        }
        JsonElement choice = choices[0];
        switch (JsonRead.ReadString(choice, "finish_reason"))
        {
            case "length":
                throw ProviderReply.Truncated();
            case "content_filter":
                throw new LlmException("The AI declined the request (content filter).", LlmFailure.Refusal);
        }
        return new LlmReply(ProviderReply.MessageContent(choice)
            ?? throw new LlmException("malformed response (no choices[0].message.content)"));
    }
}
