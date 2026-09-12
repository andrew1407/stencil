using System.Net;
using System.Text.Json;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Infrastructure.Llm;
using Stencil.TelegramBot.Tests.Doubles;
using static Stencil.TelegramBot.Tests.LlmWireRig;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The §6.3 stencil-server provider: the protocol shapes it posts under the session bearer, the
/// stop reasons that never yield a plan, and the disabled/unresolved-URL refusals.
/// </summary>
public sealed class HttpLlmServerTests
{
    [Fact]
    public async Task StencilServerPostsLlmChatWithTheSessionBearerAndProtocolShapes()
    {
        CannedHttpMessageHandler handler = new((_, _) =>
            CannedHttpMessageHandler.Json("""{"model":"claude-opus-5","text":"proxied","stopReason":"end_turn"}"""));
        HttpLlmClient client = Client(handler, new LlmOptions
        {
            Provider = LlmOptions.PROVIDER_STENCIL_SERVER,
            Model = "claude-opus-5",
        });

        LlmReply reply = await client.ChatAsync(Request(
            new LlmImage("image/png", "QUJD"), serverUrl: "https://stencil.example.com:8090", serverToken: "tok-9"));

        Assert.Equal("proxied", reply.Text);
        Assert.Equal("https://stencil.example.com:8090/llm/chat", handler.LastRequest!.RequestUri!.ToString());
        Assert.Equal("Bearer", handler.LastRequest.Headers.Authorization!.Scheme);
        Assert.Equal("tok-9", handler.LastRequest.Headers.Authorization.Parameter);

        using JsonDocument body = BodyOf(handler);
        JsonElement root = body.RootElement;
        Assert.Equal("SYSTEM PROMPT", root.GetProperty("system").GetString());
        Assert.Equal("claude-opus-5", root.GetProperty("model").GetString());
        JsonElement messages = root.GetProperty("messages");
        Assert.Equal(3, messages.GetArrayLength()); // system rides its own field, not a message
        Assert.Equal("user", messages[0].GetProperty("role").GetString());
        Assert.Equal("hello", messages[0].GetProperty("text").GetString());
        JsonElement image = messages[0].GetProperty("images")[0];
        Assert.Equal("image/png", image.GetProperty("mediaType").GetString());
        Assert.Equal("QUJD", image.GetProperty("data").GetString());
        Assert.Equal("assistant", messages[1].GetProperty("role").GetString());
        Assert.False(messages[1].TryGetProperty("images", out _));
    }

    [Theory]
    [InlineData("max_tokens", LlmFailure.Truncated)]
    [InlineData("refusal", LlmFailure.Refusal)]
    public async Task StencilServerStopReasonsBecomeFailuresNeverPlans(string stopReason, LlmFailure expected)
    {
        CannedHttpMessageHandler handler = new((_, _) =>
            CannedHttpMessageHandler.Json($$"""{"model":"m","text":"nope","stopReason":"{{stopReason}}"}"""));
        HttpLlmClient client = Client(handler, new LlmOptions { Provider = LlmOptions.PROVIDER_STENCIL_SERVER });

        LlmException ex = await Assert.ThrowsAsync<LlmException>(() =>
            client.ChatAsync(Request(serverUrl: "http://h:8090", serverToken: "t")));
        Assert.Equal(expected, ex.Failure);
    }

    [Fact]
    public async Task StencilServerLlmDisabledSurfacesTheServersMessage()
    {
        CannedHttpMessageHandler handler = new((_, _) =>
            CannedHttpMessageHandler.Json(
                """{"code":"llmDisabled","message":"LLM support is not configured on this server"}""",
                HttpStatusCode.ServiceUnavailable));
        HttpLlmClient client = Client(handler, new LlmOptions { Provider = LlmOptions.PROVIDER_STENCIL_SERVER });

        LlmException ex = await Assert.ThrowsAsync<LlmException>(() =>
            client.ChatAsync(Request(serverUrl: "http://h:8090", serverToken: "t")));
        Assert.Equal(LlmFailure.Disabled, ex.Failure);
        Assert.Contains("LLM support is not configured", ex.Message);
    }

    [Fact]
    public async Task StencilServerWithoutAResolvedUrlThrows()
    {
        HttpLlmClient client = Client(
            new CannedHttpMessageHandler((_, _) => CannedHttpMessageHandler.Json("{}")),
            new LlmOptions { Provider = LlmOptions.PROVIDER_STENCIL_SERVER });

        LlmException ex = await Assert.ThrowsAsync<LlmException>(() => client.ChatAsync(Request()));
        // The user is told the one step they can take; the env var is operator detail.
        Assert.Contains("/connect", ex.Message);
        Assert.DoesNotContain("STENCIL_", ex.Message);
        Assert.Contains("STENCIL_LLM_SERVER_URL", ex.OperatorDetail);
    }

    // An unreachable/slow endpoint is the operator's URL, never one the user typed: the chat
    // hears only that the service is down, the log gets the endpoint and the transport error.
    [Fact]
    public async Task AnUnreachableEndpointKeepsItsUrlOutOfTheMessage()
    {
        HttpLlmClient client = Client(
            new CannedHttpMessageHandler((_, _) => throw new HttpRequestException("connection refused")),
            new LlmOptions { BaseUrl = "http://ollama.internal:11434" });

        LlmException ex = await Assert.ThrowsAsync<LlmException>(() => client.ChatAsync(Request()));
        Assert.Equal("Could not reach the AI service.", ex.Message);
        Assert.DoesNotContain("ollama.internal", ex.Message);
        Assert.Contains("ollama.internal:11434", ex.OperatorDetail);
        Assert.Contains("connection refused", ex.OperatorDetail);
    }

}
