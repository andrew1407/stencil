using System.Net;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Infrastructure.Llm;
using Stencil.TelegramBot.Tests.Doubles;
using Stencil.TelegramBot.Domain.Llm.Wire;

namespace Stencil.TelegramBot.Tests;

/// <summary>The shared wire-mapping vectors through the real <see cref="HttpLlmClient"/> over a capturing <see cref="CannedHttpMessageHandler"/> — zero network, one test per vector: URL, Authorization (absence included) and the exact body are deep-compared. Wording divergences live in <c>FixtureOverrides.json</c>.</summary>
public sealed class ProviderWireFixtureWalkerTests
{
    private static readonly string[] _files = ["ollama.json", "openai.json", "server.json", "httpErrors.json"];

    private static string pathFor(string file) =>
        Path.Combine(SharedFixtures.LlmFixtureDir("providerWire"), file);

    public static TheoryData<string> Vectors() =>
        SharedFixtures.TheoryNames(_files.SelectMany(f => SharedFixtures.CaseNames(pathFor(f))));

    [Fact]
    public void Should_Have_Every_Vector_In_The_Corpus() =>
        Assert.Equal(26, _files.Sum(f => SharedFixtures.Cases(pathFor(f)).Count));

    [Theory]
    [MemberData(nameof(Vectors))]
    public async Task Should_Match_Each_Vector(string name)
    {
        string file = _files.First(f => SharedFixtures.CaseNames(pathFor(f)).Contains(name));
        using JsonDocument doc = SharedFixtures.Case(pathFor(file), name);
        List<string> failures = new();
        try
        {
            await runCaseAsync(doc.RootElement, name, failures);
        }
        catch (Exception ex)
        {
            failures.Add($"{file}/{name}: unexpected {ex.GetType().Name}: {ex.Message}");
        }
        Assert.True(failures.Count == 0, string.Join("\n", failures));
    }

    private static async Task runCaseAsync(JsonElement fx, string name, List<string> failures)
    {
        JsonElement settings = fx.GetProperty("settings");
        string provider = fx.GetProperty("provider").GetString()!;
        LlmOptions options = new()
        {
            Provider = provider switch
            {
                "ollama" => LlmOptions.PROVIDER_OLLAMA,
                "openai" => LlmOptions.PROVIDER_OPEN_AI_COMPAT,
                "server" => LlmOptions.PROVIDER_STENCIL_SERVER,
                _ => throw new InvalidOperationException($"unknown provider {provider}"),
            },
            BaseUrl = readString(settings, "baseUrl") ?? "",
            Model = readString(settings, "model") ?? "",
            ApiKey = readString(settings, "apiKey") ?? "",
        };
        LlmChatRequest request = new()
        {
            System = fx.GetProperty("chat").GetProperty("system").GetString()!,
            Messages = parseMessages(fx.GetProperty("chat").GetProperty("messages")),
            ServerUrl = readString(settings, "serverUrl"),
            ServerToken = readString(fx, "token"),
        };

        HttpResponseMessage canned = fx.TryGetProperty("response", out JsonElement response)
            ? CannedHttpMessageHandler.Json(response.GetRawText())
            : errorResponse(fx.GetProperty("errorResponse"));
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
        string? expectAuth = readString(fx, "expectAuthorization");
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
            assertError(name, error, flippedError, failures);
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
            ? kindOv.GetString() : readString(expectError, "kind"))!;
        string message = (ov is JsonElement m && m.TryGetProperty("message", out JsonElement msgOv)
            ? msgOv.GetString() : readString(expectError, "message"))!;
        using JsonDocument wanted = JsonDocument.Parse(
            $"{{\"kind\":{JsonSerializer.Serialize(kind)},\"message\":{JsonSerializer.Serialize(message)}}}");
        assertError(name, error, wanted.RootElement, failures);
    }

    private static void assertError(string name, LlmException? error, JsonElement expect, List<string> failures)
    {
        if (error is null)
        {
            failures.Add($"{name}: expected an error, got a reply");
            return;
        }
        // Kind mapping: truncated/refusal/disabled are typed; "http" and "badReply" are the bot's plain Error.
        // An unknown kind still demands an override so a new gap is recorded, not absorbed.
        string kindName = expect.GetProperty("kind").GetString()!;
        LlmFailure? want = kindName switch
        {
            "truncated" => LlmFailure.TRUNCATED,
            "refusal" => LlmFailure.REFUSAL,
            "disabled" => LlmFailure.DISABLED,
            "badReply" => LlmFailure.ERROR,
            "http" => LlmFailure.ERROR,
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

    private static HttpResponseMessage errorResponse(JsonElement spec)
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

    private static List<LlmMessage> parseMessages(JsonElement messages)
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

    private static string? readString(JsonElement element, string name) =>
        element.TryGetProperty(name, out JsonElement value) && value.ValueKind == JsonValueKind.String
            ? value.GetString()
            : null;
}
