using System.Text;
using System.Text.Json;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Infrastructure.Llm;
using Stencil.TelegramBot.Tests.Doubles;
using Stencil.TelegramBot.Domain.Llm.Wire;

namespace Stencil.TelegramBot.Tests.Llm.Wire;

/// <summary>The shared rig for the <see cref="HttpLlmClient"/> wire suites: a client over a captured-request mock handler (no network), the canonical three-message request, and the captured body as JSON.</summary>
internal static class LlmWireRig
{
    internal static HttpLlmClient Client(CannedHttpMessageHandler handler, LlmOptions options) =>
        new(new HttpClient(handler), options);

    internal static LlmChatRequest Request(LlmImage? image = null, string? serverUrl = null, string? serverToken = null) =>
        new()
        {
            System = "SYSTEM PROMPT",
            Messages =
            [
                new LlmMessage(LlmMessage.ROLE_USER, "hello", image is null ? [] : [image]),
                new LlmMessage(LlmMessage.ROLE_ASSISTANT, "prior reply"),
                new LlmMessage(LlmMessage.ROLE_USER, "again"),
            ],
            ServerUrl = serverUrl,
            ServerToken = serverToken,
        };

    internal static JsonDocument BodyOf(CannedHttpMessageHandler handler) =>
        JsonDocument.Parse(Encoding.UTF8.GetString(handler.LastBody));
}
