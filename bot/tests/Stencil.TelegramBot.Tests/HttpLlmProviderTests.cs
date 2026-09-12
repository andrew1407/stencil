using System.Net;
using System.Text.Json;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Infrastructure.Llm;
using Stencil.TelegramBot.Tests.Doubles;
using static Stencil.TelegramBot.Tests.LlmWireRig;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The two direct provider mappings of <c>llm-contract.md</c> §6 — ollama's native chat (§6.1)
/// and the openai-compatible completions (§6.2): URL, headers, body shape and stop reasons.
/// </summary>
public sealed class HttpLlmProviderTests
{
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
        Assert.Equal(LlmFailure.TRUNCATED, ex.Failure);
    }

    // ── openai-compat (§6.2) ──

    [Fact]
    public async Task OpenAiCompatPostsChatCompletionsWithBearerAndDataUrlImageParts()
    {
        CannedHttpMessageHandler handler = new((_, _) =>
            CannedHttpMessageHandler.Json("""{"choices":[{"message":{"content":"lm reply"},"finish_reason":"stop"}]}"""));
        HttpLlmClient client = Client(handler, new LlmOptions
        {
            Provider = LlmOptions.PROVIDER_OPEN_AI_COMPAT,
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
        HttpLlmClient client = Client(handler, new LlmOptions { Provider = LlmOptions.PROVIDER_OPEN_AI_COMPAT });

        await client.ChatAsync(Request());

        Assert.Null(handler.LastRequest!.Headers.Authorization);
    }

    [Theory]
    [InlineData("length", LlmFailure.TRUNCATED)]
    [InlineData("content_filter", LlmFailure.REFUSAL)]
    public async Task OpenAiCompatMapsFinishReasonsToFailures(string finishReason, LlmFailure expected)
    {
        CannedHttpMessageHandler handler = new((_, _) =>
            CannedHttpMessageHandler.Json(
                $$"""{"choices":[{"message":{"content":"partial"},"finish_reason":"{{finishReason}}"}]}"""));
        HttpLlmClient client = Client(handler, new LlmOptions { Provider = LlmOptions.PROVIDER_OPEN_AI_COMPAT });

        LlmException ex = await Assert.ThrowsAsync<LlmException>(() => client.ChatAsync(Request()));
        Assert.Equal(expected, ex.Failure);
    }

}
