using System.Net;
using System.Text;
using System.Text.Json;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Infrastructure.Llm;
using Stencil.TelegramBot.Tests.Doubles;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// Wire-contract behaviour for <see cref="HttpLlmClient"/> against a captured-request mock
/// handler (no network): the three provider mappings of <c>llm-contract.md</c> §6 —
/// request URL/headers/body shape, response parsing, error mapping, and the
/// <c>max_tokens</c>/<c>refusal</c> stop reasons that must never yield a parseable reply.
/// </summary>
public sealed class HttpLlmClientTests
{
    private static HttpLlmClient Client(CannedHttpMessageHandler handler, LlmOptions options) =>
        new(new HttpClient(handler), options);

    private static LlmChatRequest Request(LlmImage? image = null, string? serverUrl = null, string? serverToken = null) =>
        new()
        {
            System = "SYSTEM PROMPT",
            Messages =
            [
                new LlmMessage(LlmMessage.RoleUser, "hello", image is null ? [] : [image]),
                new LlmMessage(LlmMessage.RoleAssistant, "prior reply"),
                new LlmMessage(LlmMessage.RoleUser, "again"),
            ],
            ServerUrl = serverUrl,
            ServerToken = serverToken,
        };

    private static JsonDocument BodyOf(CannedHttpMessageHandler handler) =>
        JsonDocument.Parse(Encoding.UTF8.GetString(handler.LastBody));

    // ── ollama (§6.1) ──

    [Fact]
    public async Task OllamaPostsNativeChatWithSystemFirstAndBareBase64Images()
    {
        CannedHttpMessageHandler handler = new((_, _) =>
            CannedHttpMessageHandler.Json("""{"message":{"role":"assistant","content":"the reply"},"done":true}"""));
        HttpLlmClient client = Client(handler, new LlmOptions { Model = "llava" });

        LlmReply reply = await client.ChatAsync(Request(new LlmImage("image/png", "QUJD")));

        Assert.Equal("the reply", reply.Text);
        Assert.Equal(HttpMethod.Post, handler.LastRequest!.Method);
        Assert.Equal("http://localhost:11434/api/chat", handler.LastRequest.RequestUri!.ToString());
        Assert.Null(handler.LastRequest.Headers.Authorization);
        Assert.Equal("application/json", handler.LastContentType);

        using JsonDocument body = BodyOf(handler);
        JsonElement root = body.RootElement;
        Assert.Equal("llava", root.GetProperty("model").GetString());
        Assert.False(root.GetProperty("stream").GetBoolean());
        JsonElement messages = root.GetProperty("messages");
        Assert.Equal(4, messages.GetArrayLength()); // system + 3 turns
        Assert.Equal("system", messages[0].GetProperty("role").GetString());
        Assert.Equal("SYSTEM PROMPT", messages[0].GetProperty("content").GetString());
        Assert.Equal("hello", messages[1].GetProperty("content").GetString());
        Assert.Equal("QUJD", messages[1].GetProperty("images")[0].GetString());
        Assert.False(messages[2].TryGetProperty("images", out _)); // text-only ⇒ no images field
    }

    [Fact]
    public async Task OllamaLengthDoneReasonThrowsTruncated()
    {
        CannedHttpMessageHandler handler = new((_, _) =>
            CannedHttpMessageHandler.Json("""{"message":{"content":"partial"},"done_reason":"length"}"""));
        HttpLlmClient client = Client(handler, new LlmOptions());

        LlmException ex = await Assert.ThrowsAsync<LlmException>(() => client.ChatAsync(Request()));
        Assert.Equal(LlmFailure.Truncated, ex.Failure);
    }

    // ── openai-compat (§6.2) ──

    [Fact]
    public async Task OpenAiCompatPostsChatCompletionsWithBearerAndDataUrlImageParts()
    {
        CannedHttpMessageHandler handler = new((_, _) =>
            CannedHttpMessageHandler.Json("""{"choices":[{"message":{"content":"lm reply"},"finish_reason":"stop"}]}"""));
        HttpLlmClient client = Client(handler, new LlmOptions
        {
            Provider = LlmOptions.ProviderOpenAiCompat,
            BaseUrl = LlmOptions.DefaultOpenAiCompatBaseUrl,
            ApiKey = "sk-test",
        });

        LlmReply reply = await client.ChatAsync(Request(new LlmImage("image/jpeg", "QUJD")));

        Assert.Equal("lm reply", reply.Text);
        Assert.Equal("http://localhost:1234/v1/chat/completions", handler.LastRequest!.RequestUri!.ToString());
        Assert.Equal("Bearer", handler.LastRequest.Headers.Authorization!.Scheme);
        Assert.Equal("sk-test", handler.LastRequest.Headers.Authorization.Parameter);

        using JsonDocument body = BodyOf(handler);
        JsonElement messages = body.RootElement.GetProperty("messages");
        Assert.Equal("system", messages[0].GetProperty("role").GetString());
        // With an image the content is a parts array: text first, then the data: URL.
        JsonElement parts = messages[1].GetProperty("content");
        Assert.Equal(JsonValueKind.Array, parts.ValueKind);
        Assert.Equal("text", parts[0].GetProperty("type").GetString());
        Assert.Equal("hello", parts[0].GetProperty("text").GetString());
        Assert.Equal("data:image/jpeg;base64,QUJD",
            parts[1].GetProperty("image_url").GetProperty("url").GetString());
        // A text-only message stays a plain string.
        Assert.Equal(JsonValueKind.String, messages[2].GetProperty("content").ValueKind);
    }

    [Fact]
    public async Task OpenAiCompatWithoutAnApiKeySendsNoAuthorizationHeader()
    {
        CannedHttpMessageHandler handler = new((_, _) =>
            CannedHttpMessageHandler.Json("""{"choices":[{"message":{"content":"x"}}]}"""));
        HttpLlmClient client = Client(handler, new LlmOptions { Provider = LlmOptions.ProviderOpenAiCompat });

        await client.ChatAsync(Request());

        Assert.Null(handler.LastRequest!.Headers.Authorization);
    }

    [Theory]
    [InlineData("length", LlmFailure.Truncated)]
    [InlineData("content_filter", LlmFailure.Refusal)]
    public async Task OpenAiCompatMapsFinishReasonsToFailures(string finishReason, LlmFailure expected)
    {
        CannedHttpMessageHandler handler = new((_, _) =>
            CannedHttpMessageHandler.Json(
                $$"""{"choices":[{"message":{"content":"partial"},"finish_reason":"{{finishReason}}"}]}"""));
        HttpLlmClient client = Client(handler, new LlmOptions { Provider = LlmOptions.ProviderOpenAiCompat });

        LlmException ex = await Assert.ThrowsAsync<LlmException>(() => client.ChatAsync(Request()));
        Assert.Equal(expected, ex.Failure);
    }

    // ── stencil-server (§6.3) ──

    [Fact]
    public async Task StencilServerPostsLlmChatWithTheSessionBearerAndProtocolShapes()
    {
        CannedHttpMessageHandler handler = new((_, _) =>
            CannedHttpMessageHandler.Json("""{"model":"claude-opus-5","text":"proxied","stopReason":"end_turn"}"""));
        HttpLlmClient client = Client(handler, new LlmOptions
        {
            Provider = LlmOptions.ProviderStencilServer,
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
        HttpLlmClient client = Client(handler, new LlmOptions { Provider = LlmOptions.ProviderStencilServer });

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
        HttpLlmClient client = Client(handler, new LlmOptions { Provider = LlmOptions.ProviderStencilServer });

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
            new LlmOptions { Provider = LlmOptions.ProviderStencilServer });

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

    // ── shared error mapping ──

    [Fact]
    public async Task NonSuccessWithAnOpenAiStyleErrorBodyIsSurfaced()
    {
        CannedHttpMessageHandler handler = new((_, _) =>
            CannedHttpMessageHandler.Json("""{"error":{"message":"model not found"}}""", HttpStatusCode.NotFound));
        HttpLlmClient client = Client(handler, new LlmOptions());

        LlmException ex = await Assert.ThrowsAsync<LlmException>(() => client.ChatAsync(Request()));
        // Said once (contract §6.3): the provider's sentence, with nothing wrapped
        // around it and no status restating it.
        Assert.Equal("model not found", ex.Message);
    }

    [Fact]
    public async Task AnUpstreamReasonIsSentAsItself()
    {
        CannedHttpMessageHandler handler = new((_, _) =>
            CannedHttpMessageHandler.Json(
                """{"code":"llmUpstream","message":"the LLM provider is out of credits or has no active billing"}""",
                HttpStatusCode.BadGateway));
        HttpLlmClient client = Client(handler, new LlmOptions { Provider = LlmOptions.ProviderStencilServer });

        LlmException ex = await Assert.ThrowsAsync<LlmException>(() =>
            client.ChatAsync(Request(serverUrl: "http://h:8090", serverToken: "t")));
        Assert.Equal("the LLM provider is out of credits or has no active billing", ex.Message);
    }

    [Fact]
    public async Task AnErrorBodyThatSaysNothingFallsBackToTheStatus()
    {
        CannedHttpMessageHandler handler = new((_, _) =>
            CannedHttpMessageHandler.Json("""{"nothing":"useful"}""", HttpStatusCode.BadGateway));
        HttpLlmClient client = Client(handler, new LlmOptions());

        LlmException ex = await Assert.ThrowsAsync<LlmException>(() => client.ChatAsync(Request()));
        Assert.Equal("The LLM endpoint answered HTTP 502.", ex.Message);
    }

    [Theory]
    [InlineData("model\nnot\tfound", "model not found")]
    [InlineData("Incorrect API key provided: sk-abcdef1234567890", "Incorrect API key provided: [redacted]")]
    [InlineData("failed to reach http://10.0.0.5:11434/api/chat now", "failed to reach [redacted] now")]
    public void ProviderProseIsControlFreeAndNeverEchoesAKeyOrUrl(string raw, string expected)
    {
        Assert.Equal(expected, HttpLlmClient.SanitizeProviderText(raw));
    }

    [Fact]
    public void ProviderProseIsHardTruncated()
    {
        string cut = HttpLlmClient.SanitizeProviderText(string.Concat(Enumerable.Repeat("the model is very busy right now. ", 30)));
        Assert.True(cut.Length <= HttpLlmClient.MaxProviderDetail);
        Assert.EndsWith("…", cut);
    }

    [Fact]
    public async Task UnknownProviderThrowsAConfigurationError()
    {
        HttpLlmClient client = Client(
            new CannedHttpMessageHandler((_, _) => CannedHttpMessageHandler.Json("{}")),
            new LlmOptions { Provider = "clippy" });

        LlmException ex = await Assert.ThrowsAsync<LlmException>(() => client.ChatAsync(Request()));
        // A misconfigured provider is the operator's problem: plain sentence out, detail logged.
        Assert.DoesNotContain("clippy", ex.Message);
        Assert.Contains("clippy", ex.OperatorDetail);
        Assert.Contains("STENCIL_LLM_PROVIDER", ex.OperatorDetail);
    }
}
