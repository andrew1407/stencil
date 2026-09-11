using System.Text.Json;
using System.Text.Json.Nodes;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Serialization;

namespace Stencil.TelegramBot.Infrastructure.Llm;

/// <summary>§6.3 stencil-server: <c>POST {serverUrl}/llm/chat</c> with the user's session
/// bearer. The caller resolves URL + token, so this stays free of session logic.</summary>
internal sealed class StencilServerMapping : IProviderMapping
{
    public string Url(LlmChatRequest request, LlmOptions options) => ServerUrl(request).TrimEnd('/') + "/llm/chat";

    public string? Bearer(LlmChatRequest request, LlmOptions options) => request.ServerToken ?? "";

    private static string ServerUrl(LlmChatRequest request)
    {
        if (request.ServerUrl is not string serverUrl || serverUrl.Length == 0)
        {
            // What to configure is operator business; the user gets the one step they can take.
            throw LlmException.Deployment(
                "The AI assistant has no Stencil server to talk to — /connect one first.",
                "no Stencil server resolved for the LLM proxy — the user has no connection and "
                + "STENCIL_LLM_SERVER_URL is unset");
        }
        return serverUrl;
    }

    public JsonObject Body(LlmChatRequest request, LlmOptions options)
    {
        JsonArray messages = new();
        foreach (LlmMessage message in request.Messages)
        {
            JsonObject entry = new() { ["role"] = message.Role, ["text"] = message.Text };
            if (message.Images.Count > 0)
            {
                JsonArray images = new();
                foreach (LlmImage image in message.Images)
                {
                    images.Add(new JsonObject { ["mediaType"] = image.MediaType, ["data"] = image.Base64Data });
                }
                entry["images"] = images;
            }
            messages.Add(entry);
        }
        JsonObject body = new()
        {
            ["system"] = request.System,
            ["messages"] = messages,
        };
        if (options.Model.Length > 0)
        {
            body["model"] = options.Model;
        }
        return body;
    }

    public LlmReply Read(JsonElement root)
    {
        // Null when the text field is absent/not a string — "" stays a (blank) reply.
        string? text = root.TryGetProperty("text", out JsonElement t) && t.ValueKind == JsonValueKind.String
            ? t.GetString() ?? ""
            : null;
        switch (JsonRead.ReadString(root, "stopReason"))
        {
            case "max_tokens":
                throw ProviderReply.Truncated();
            case "refusal":
                throw new LlmException(
                    string.IsNullOrEmpty(text) ? "The AI declined the request." : $"The AI declined: {text}",
                    LlmFailure.Refusal);
        }
        return new LlmReply(text ?? throw new LlmException("malformed server response (no text)"));
    }
}
