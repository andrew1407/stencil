using System.Net;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Infrastructure.Llm;
using Stencil.TelegramBot.Tests.Doubles;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// Walks the shared provider wire-mapping vectors
/// (<c>browser/js/config/llm/fixtures/providerWire/</c>, see <c>_schema.md</c>) through the
/// REAL <see cref="HttpLlmClient"/> pointed at a capturing
/// <see cref="CannedHttpMessageHandler"/> — zero network. Request URL, Authorization header
/// (absence included) and the exact JSON body are deep-compared; the canned
/// response/errorResponse then drives reply extraction / typed errors. Message wording
/// divergences are pinned per-case in <c>FixtureOverrides.json</c> (the one structural
/// gap left: <see cref="LlmException"/> carries no HTTP status, so expectError.status is
/// untestable here).
/// </summary>
public sealed class ProviderWireFixtureWalkerTests
{
    private static readonly string[] Files = ["ollama.json", "openai.json", "server.json", "httpErrors.json"];

    [Fact]
    public async Task EveryWireVectorMatches()
    {
        List<string> failures = new();
        int walked = 0;
        foreach (string file in Files)
        {
            using JsonDocument doc = SharedFixtures.Load(
                Path.Combine(SharedFixtures.LlmFixtureDir("providerWire"), file));
            foreach (JsonElement fx in doc.RootElement.EnumerateArray())
            {
                walked++;
                string name = fx.GetProperty("name").GetString()!;
                try
                {
                    await RunCaseAsync(fx, name, failures);
                }
                catch (Exception ex)
                {
                    failures.Add($"{file}/{name}: unexpected {ex.GetType().Name}: {ex.Message}");
                }
            }
        }
        Assert.Equal(26, walked);
        Assert.True(failures.Count == 0,
            $"{failures.Count} wire mismatches (walked {walked}):\n" + string.Join("\n", failures));
    }

    private static async Task RunCaseAsync(JsonElement fx, string name, List<string> failures)
    {
        JsonElement settings = fx.GetProperty("settings");
        string provider = fx.GetProperty("provider").GetString()!;
        LlmOptions options = new()
        {
            Provider = provider switch
            {
                "ollama" => LlmOptions.ProviderOllama,
                "openai" => LlmOptions.ProviderOpenAiCompat,
                "server" => LlmOptions.ProviderStencilServer,
                _ => throw new InvalidOperationException($"unknown provider {provider}"),
            },
            BaseUrl = ReadString(settings, "baseUrl") ?? "",
            Model = ReadString(settings, "model") ?? "",
            ApiKey = ReadString(settings, "apiKey") ?? "",
        };
        LlmChatRequest request = new()
        {
            System = fx.GetProperty("chat").GetProperty("system").GetString()!,
            Messages = ParseMessages(fx.GetProperty("chat").GetProperty("messages")),
            ServerUrl = ReadString(settings, "serverUrl"),
            ServerToken = ReadString(fx, "token"),
        };

        HttpResponseMessage canned = fx.TryGetProperty("response", out JsonElement response)
            ? CannedHttpMessageHandler.Json(response.GetRawText())
            : ErrorResponse(fx.GetProperty("errorResponse"));
        CannedHttpMessageHandler handler = new((_, _) => canned);
        HttpLlmClient client = new(new HttpClient(handler), options);

        LlmReply? reply = null;
        LlmException? error = null;
        try
        {
            reply = await client.ChatAsync(request);
        }
        catch (LlmException ex)
        {
            error = ex;
        }

        // The request side: URL, method, Authorization (absence is contract), exact body.
        HttpRequestMessage sent = handler.LastRequest!;
        if (sent.Method != HttpMethod.Post)
        {
            failures.Add($"{name}: expected POST, sent {sent.Method}");
        }
        string expectUrl = fx.GetProperty("expectUrl").GetString()!;
        if (sent.RequestUri!.ToString() != expectUrl)
        {
            failures.Add($"{name}: URL {sent.RequestUri} != {expectUrl}");
        }
        if (handler.LastContentType != "application/json")
        {
            failures.Add($"{name}: content type {handler.LastContentType} != application/json");
        }
        string? sentAuth = sent.Headers.Authorization?.ToString();
        string? expectAuth = ReadString(fx, "expectAuthorization");
        if (sentAuth != expectAuth)
        {
            failures.Add($"{name}: Authorization \"{sentAuth ?? "<none>"}\" != \"{expectAuth ?? "<none>"}\"");
        }
        JsonNode? sentBody = JsonNode.Parse(Encoding.UTF8.GetString(handler.LastBody));
        JsonNode? expectBody = JsonNode.Parse(fx.GetProperty("expectBody").GetRawText());
        if (!JsonNode.DeepEquals(sentBody, expectBody))
        {
            failures.Add($"{name}: body\n  sent   {sentBody?.ToJsonString()}\n  expect {expectBody?.ToJsonString()}");
        }

        // The response side, override-aware (see FixtureOverrides.json).
        JsonElement? ov = SharedFixtures.OverrideFor("providerWire", name);
        if (ov is JsonElement o && o.TryGetProperty("error", out JsonElement flippedError))
        {
            // The bot errors where the browser extracts a reply.
            AssertError(name, error, flippedError, failures);
            return;
        }
        if (fx.TryGetProperty("expectReply", out JsonElement expectReply))
        {
            if (error is not null)
            {
                failures.Add($"{name}: expected reply, got error \"{error.Message}\"");
            }
            else if (reply!.Text != expectReply.GetString())
            {
                failures.Add($"{name}: reply \"{reply.Text}\" != \"{expectReply.GetString()}\"");
            }
            return;
        }
        JsonElement expectError = fx.GetProperty("expectError");
        string kind = (ov is JsonElement k && k.TryGetProperty("kind", out JsonElement kindOv)
            ? kindOv.GetString() : ReadString(expectError, "kind"))!;
        string message = (ov is JsonElement m && m.TryGetProperty("message", out JsonElement msgOv)
            ? msgOv.GetString() : ReadString(expectError, "message"))!;
        using JsonDocument wanted = JsonDocument.Parse(
            $"{{\"kind\":{JsonSerializer.Serialize(kind)},\"message\":{JsonSerializer.Serialize(message)}}}");
        AssertError(name, error, wanted.RootElement, failures);
    }

    private static void AssertError(string name, LlmException? error, JsonElement expect, List<string> failures)
    {
        if (error is null)
        {
            failures.Add($"{name}: expected an error, got a reply");
            return;
        }
        // Kind mapping: truncated/refusal/disabled are typed; "http" and "badReply" are
        // the bot's plain Error (its bad replies carry the shared message, no own kind).
        // An unknown kind still demands an override so a new gap is recorded, not absorbed.
        string kindName = expect.GetProperty("kind").GetString()!;
        LlmFailure? want = kindName switch
        {
            "truncated" => LlmFailure.Truncated,
            "refusal" => LlmFailure.Refusal,
            "disabled" => LlmFailure.Disabled,
            "badReply" => LlmFailure.Error,
            "http" => LlmFailure.Error,
            _ => null,
        };
        if (want is null)
        {
            failures.Add($"{name}: kind \"{kindName}\" has no bot equivalent — override required");
            return;
        }
        if (error.Failure != want)
        {
            failures.Add($"{name}: failure kind {error.Failure} != {want}");
        }
        string message = expect.GetProperty("message").GetString()!;
        if (error.Message != message)
        {
            failures.Add($"{name}: error message \"{error.Message}\" != \"{message}\"");
        }
    }

    private static HttpResponseMessage ErrorResponse(JsonElement spec)
    {
        var status = (HttpStatusCode)spec.GetProperty("status").GetInt32();
        JsonElement body = spec.GetProperty("body");
        return body.ValueKind == JsonValueKind.String
            ? new HttpResponseMessage(status)
            {
                Content = new StringContent(body.GetString()!, Encoding.UTF8, "text/html"),
            }
            : CannedHttpMessageHandler.Json(body.GetRawText(), status);
    }

    private static List<LlmMessage> ParseMessages(JsonElement messages)
    {
        List<LlmMessage> parsed = new();
        foreach (JsonElement m in messages.EnumerateArray())
        {
            List<LlmImage> images = new();
            if (m.TryGetProperty("images", out JsonElement list))
            {
                foreach (JsonElement image in list.EnumerateArray())
                {
                    images.Add(new LlmImage(
                        image.GetProperty("mediaType").GetString()!,
                        image.GetProperty("data").GetString()!));
                }
            }
            parsed.Add(new LlmMessage(
                m.GetProperty("role").GetString()!, m.GetProperty("text").GetString()!, images));
        }
        return parsed;
    }

    private static string? ReadString(JsonElement element, string name) =>
        element.TryGetProperty(name, out JsonElement value) && value.ValueKind == JsonValueKind.String
            ? value.GetString()
            : null;
}
