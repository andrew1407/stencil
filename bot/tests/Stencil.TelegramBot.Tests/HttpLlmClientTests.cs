using System.Net;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Infrastructure.Llm;
using Stencil.TelegramBot.Tests.Doubles;
using static Stencil.TelegramBot.Tests.LlmWireRig;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The error mapping <see cref="HttpLlmClient"/> shares across every provider: what reaches the
/// user, what stays in the operator detail, and the sanitizer that keeps a key or URL out of it.
/// </summary>
public sealed class HttpLlmClientTests
{
    [Fact]
    public async Task Should_Surface_A_Non_Success_With_An_Open_Ai_Style_Error_Body()
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
    public async Task Should_Send_An_Upstream_Reason_As_Itself()
    {
        CannedHttpMessageHandler handler = new((_, _) =>
            CannedHttpMessageHandler.Json(
                """{"code":"llmUpstream","message":"the LLM provider is out of credits or has no active billing"}""",
                HttpStatusCode.BadGateway));
        HttpLlmClient client = Client(handler, new LlmOptions { Provider = LlmOptions.PROVIDER_STENCIL_SERVER });

        LlmException ex = await Assert.ThrowsAsync<LlmException>(() =>
            client.ChatAsync(Request(serverUrl: "http://h:8090", serverToken: "t")));
        Assert.Equal("the LLM provider is out of credits or has no active billing", ex.Message);
    }

    [Fact]
    public async Task Should_Fall_Back_To_The_Status_For_An_Error_Body_That_Says_Nothing()
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
    public void Should_Keep_Provider_Prose_Control_Free_And_Never_Echo_A_Key_Or_Url(string raw, string expected)
    {
        Assert.Equal(expected, HttpLlmClient.SanitizeProviderText(raw));
    }

    [Fact]
    public void Should_Hard_Truncate_Provider_Prose()
    {
        string cut = HttpLlmClient.SanitizeProviderText(string.Concat(Enumerable.Repeat("the model is very busy right now. ", 30)));
        Assert.True(cut.Length <= HttpLlmClient.MAX_PROVIDER_DETAIL);
        Assert.EndsWith("…", cut);
    }

    [Fact]
    public async Task Should_Throw_A_Configuration_Error_For_An_Unknown_Provider()
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
