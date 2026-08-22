using System.Net;
using System.Net.Http.Headers;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Text.Json.Nodes;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Serialization;

namespace Stencil.TelegramBot.Infrastructure.Llm;

/// <summary>
/// The provider adapter implementing the three wire mappings of <c>llm-contract.md</c> §6:
/// <c>ollama</c> native chat (<c>POST {baseUrl}/api/chat</c>), <c>openai-compat</c>
/// (<c>POST {baseUrl}/chat/completions</c>, optional bearer key) and <c>stencil-server</c>
/// (<c>POST {serverUrl}/llm/chat</c>, the user's existing session bearer — the caller resolves
/// URL + token, this class stays free of session logic).
/// </summary>
/// <remarks>
/// The <see cref="HttpClient"/> is supplied by the caller (like
/// <c>HttpStencilServerClient</c>), so tests inject a stub message handler. Non-2xx responses,
/// unreachable endpoints and unparseable payloads become <see cref="LlmException"/>s; the
/// contract's <c>max_tokens</c>/<c>refusal</c> stop reasons become
/// <see cref="LlmFailure.Truncated"/>/<see cref="LlmFailure.Refusal"/> so a truncated or
/// refused reply is never parsed as a plan.
/// </remarks>
public sealed class HttpLlmClient : ILlmClient
{
    /// <summary>LLM calls are slow — the canonical <c>providers.json</c> <c>timeouts.chatSeconds</c>.</summary>
    public static readonly TimeSpan DefaultTimeout = TimeSpan.FromSeconds(ProvidersAsset.ChatTimeoutSeconds);

    private readonly HttpClient _http;
    private readonly LlmOptions _options;

    public HttpLlmClient(HttpClient http, LlmOptions options)
    {
        _http = http;
        _options = options;
    }

    /// <inheritdoc />
    public Task<LlmReply> ChatAsync(LlmChatRequest request, CancellationToken ct = default)
    {
        // The call's own config when the user picked a profile (/chatapi), else the operator's.
        LlmOptions options = request.Options ?? _options;
        return options.Provider switch
        {
            LlmOptions.ProviderOllama => OllamaChatAsync(request, options, ct),
            LlmOptions.ProviderOpenAiCompat => OpenAiChatAsync(request, options, ct),
            LlmOptions.ProviderStencilServer => ServerChatAsync(request, options, ct),
            _ => throw LlmException.Deployment(
                "The AI assistant isn't configured on this bot.",
                $"unknown LLM provider \"{options.Provider}\" — set STENCIL_LLM_PROVIDER to "
                + "ollama, openai-compat or stencil-server"),
        };
    }

    // ── ollama (§6.1) ──

    private async Task<LlmReply> OllamaChatAsync(LlmChatRequest request, LlmOptions options, CancellationToken ct)
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
        JsonObject body = new()
        {
            ["model"] = options.Model,
            ["stream"] = false,
            ["messages"] = messages,
        };
        using JsonDocument doc = await PostAsync(BaseUrl(options) + "/api/chat", body, bearer: null, ct).ConfigureAwait(false);
        JsonElement root = doc.RootElement;
        // Ollama reports a truncated generation as done_reason "length".
        if (JsonRead.ReadString(root, "done_reason") == "length")
        {
            throw Truncated();
        }
        // A 2xx body without a reply string is a typed bad-reply error, never "".
        return new LlmReply(MessageContent(root)
            ?? throw new LlmException("malformed ollama response (no message.content)"));
    }

    // ── openai-compat (§6.2) ──

    private async Task<LlmReply> OpenAiChatAsync(LlmChatRequest request, LlmOptions options, CancellationToken ct)
    {
        JsonArray messages = new() { new JsonObject { ["role"] = "system", ["content"] = request.System } };
        foreach (LlmMessage message in request.Messages)
        {
            messages.Add(new JsonObject { ["role"] = message.Role, ["content"] = OpenAiContent(message) });
        }
        JsonObject body = new()
        {
            ["model"] = options.Model,
            ["stream"] = false,
            ["messages"] = messages,
        };
        string? bearer = options.ApiKey.Length == 0 ? null : options.ApiKey;
        using JsonDocument doc = await PostAsync(BaseUrl(options) + "/chat/completions", body, bearer, ct).ConfigureAwait(false);
        JsonElement root = doc.RootElement;
        if (!root.TryGetProperty("choices", out JsonElement choices) || choices.ValueKind != JsonValueKind.Array
            || choices.GetArrayLength() == 0)
        {
            throw new LlmException("malformed response (no choices[0].message.content)");
        }
        JsonElement choice = choices[0];
        switch (JsonRead.ReadString(choice, "finish_reason"))
        {
            case "length":
                throw Truncated();
            case "content_filter":
                throw new LlmException("The AI declined the request (content filter).", LlmFailure.Refusal);
        }
        return new LlmReply(MessageContent(choice)
            ?? throw new LlmException("malformed response (no choices[0].message.content)"));
    }

    /// <summary>Plain string for a text-only message; the text + image_url parts array otherwise.</summary>
    private static JsonNode OpenAiContent(LlmMessage message)
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

    // ── stencil-server (§6.3) ──

    private async Task<LlmReply> ServerChatAsync(LlmChatRequest request, LlmOptions options, CancellationToken ct)
    {
        if (request.ServerUrl is not string serverUrl || serverUrl.Length == 0)
        {
            // What to configure is operator business; the user gets the one step they can take.
            throw LlmException.Deployment(
                "The AI assistant has no Stencil server to talk to — /connect one first.",
                "no Stencil server resolved for the LLM proxy — the user has no connection and "
                + "STENCIL_LLM_SERVER_URL is unset");
        }
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
        string url = serverUrl.TrimEnd('/') + "/llm/chat";
        using JsonDocument doc = await PostAsync(url, body, request.ServerToken ?? "", ct).ConfigureAwait(false);
        JsonElement root = doc.RootElement;
        // Null when the text field is absent/not a string — "" stays a (blank) reply.
        string? text = root.TryGetProperty("text", out JsonElement t) && t.ValueKind == JsonValueKind.String
            ? t.GetString() ?? ""
            : null;
        switch (JsonRead.ReadString(root, "stopReason"))
        {
            case "max_tokens":
                throw Truncated();
            case "refusal":
                throw new LlmException(
                    string.IsNullOrEmpty(text) ? "The AI declined the request." : $"The AI declined: {text}",
                    LlmFailure.Refusal);
        }
        return new LlmReply(text ?? throw new LlmException("malformed server response (no text)"));
    }

    // ── shared plumbing ──

    private static string BaseUrl(LlmOptions options) => options.BaseUrl.TrimEnd('/');

    /// <summary>
    /// The reply text at <c>{parent}.message.content</c> (ollama root / OpenAI choice), or
    /// null when it is absent / not a string — a typed bad-reply seam, never a silent "".
    /// </summary>
    private static string? MessageContent(JsonElement parent) =>
        parent.TryGetProperty("message", out JsonElement message) && message.ValueKind == JsonValueKind.Object
            && message.TryGetProperty("content", out JsonElement content)
            && content.ValueKind == JsonValueKind.String
            ? content.GetString() ?? ""
            : null;

    private static LlmException Truncated() => new(
        "The AI response was cut off at the token limit — try a shorter or simpler request.",
        LlmFailure.Truncated);

    /// <summary>POST a JSON body (with an optional bearer) and parse the JSON response.</summary>
    private async Task<JsonDocument> PostAsync(string url, JsonObject body, string? bearer, CancellationToken ct)
    {
        // The body is streamed straight onto the request (JsonNodeContent) — with multi-MB
        // base64 images an intermediate ToJsonString() would put whole copies on the LOH.
        using HttpRequestMessage message = new(HttpMethod.Post, url)
        {
            Content = new JsonNodeContent(body),
        };
        if (bearer is not null)
        {
            message.Headers.Authorization = new AuthenticationHeaderValue("Bearer", bearer);
        }
        HttpResponseMessage response;
        try
        {
            response = await _http.SendAsync(message, ct).ConfigureAwait(false);
        }
        catch (HttpRequestException ex)
        {
            // The endpoint is the operator's configuration, not something the user typed.
            throw LlmException.Deployment(
                "Could not reach the AI service.", $"could not reach the LLM endpoint at {url}: {ex.Message}");
        }
        catch (TaskCanceledException) when (!ct.IsCancellationRequested)
        {
            throw LlmException.Deployment(
                "The AI service timed out.", $"the LLM endpoint at {url} timed out");
        }
        using (response)
        {
            byte[] bytes = await response.Content.ReadAsByteArrayAsync(ct).ConfigureAwait(false);
            if (!response.IsSuccessStatusCode)
            {
                throw ErrorFor((int)response.StatusCode, bytes);
            }
            try
            {
                return JsonDocument.Parse(bytes);
            }
            catch (JsonException)
            {
                throw new LlmException("The LLM endpoint returned a non-JSON response.");
            }
        }
    }

    /// <summary>
    /// The typed failure for a non-2xx response: a human-readable line from the error body
    /// (the shared <see cref="JsonRead.ErrorDetail"/> shapes), or the bare HTTP status;
    /// the server's <c>llmDisabled</c> code types as <see cref="LlmFailure.Disabled"/>.
    /// </summary>
    /// <remarks>
    /// The reason is said ONCE (llm-contract.md §6.3): a provider that explains itself is
    /// quoted as-is — no "request failed" preamble around the sentence and no status
    /// restating it. Only a body that says nothing falls back to the status.
    /// </remarks>
    private static LlmException ErrorFor(int status, byte[] body)
    {
        string detail = "";
        bool disabled = false;
        if (body.Length > 0)
        {
            try
            {
                using JsonDocument doc = JsonDocument.Parse(body);
                detail = SanitizeProviderText(JsonRead.ErrorDetail(doc.RootElement));
                disabled = JsonRead.ReadString(doc.RootElement, "code") == "llmDisabled";
            }
            catch (JsonException)
            {
                // Non-JSON error body — keep the bare status message.
            }
        }
        return new LlmException(
            detail.Length == 0 ? $"The LLM endpoint answered HTTP {status}." : detail,
            disabled ? LlmFailure.Disabled : LlmFailure.Error);
    }

    /// <summary>How much of a provider's own prose an error may quote.</summary>
    public const int MaxProviderDetail = 200;

    private static readonly Regex Controlish = new(@"[\p{Cc}\p{Cf}]", RegexOptions.Compiled);
    private static readonly Regex Urlish = new(@"[a-z][a-z0-9+.-]*://\S+", RegexOptions.Compiled | RegexOptions.IgnoreCase);
    private static readonly Regex Secretish = new(
        @"(?:bearer|basic) +[A-Za-z0-9._~+/=-]{8,}"
        + @"|\b(?:sk|pk|api[-_]?key|key|token|secret)[-_=:][A-Za-z0-9._-]{6,}"
        + @"|[A-Za-z0-9_-]{24,}",
        RegexOptions.Compiled | RegexOptions.IgnoreCase);

    /// <summary>
    /// Untrusted provider prose made safe to send to a chat: control characters out, URLs
    /// and token-shaped runs redacted (an endpoint may echo the key back), whitespace
    /// collapsed, hard-truncated. Port of the server's <c>sanitizeUpstreamText</c>.
    /// </summary>
    public static string SanitizeProviderText(string text)
    {
        if (text.Length == 0)
        {
            return "";
        }
        string t = text.Length > 4 * MaxProviderDetail ? text[..(4 * MaxProviderDetail)] : text;
        t = Controlish.Replace(t, " ");
        t = Secretish.Replace(Urlish.Replace(t, "[redacted]"), "[redacted]");
        t = string.Join(' ', t.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries));
        return t.Length <= MaxProviderDetail ? t : t[..(MaxProviderDetail - 1)].TrimEnd() + "…";
    }

    /// <summary>
    /// An <c>application/json</c> body that streams a <see cref="JsonObject"/> straight to the
    /// request stream via <see cref="Utf8JsonWriter"/> — no intermediate string.
    /// </summary>
    private sealed class JsonNodeContent : HttpContent
    {
        private readonly JsonObject _body;

        public JsonNodeContent(JsonObject body)
        {
            _body = body;
            Headers.ContentType = new MediaTypeHeaderValue("application/json") { CharSet = "utf-8" };
        }

        protected override Task SerializeToStreamAsync(Stream stream, TransportContext? context)
        {
            using Utf8JsonWriter writer = new(stream);
            _body.WriteTo(writer);
            return Task.CompletedTask;
        }

        protected override bool TryComputeLength(out long length)
        {
            length = -1;
            return false; // unknown up front — the request goes out chunked
        }
    }
}
