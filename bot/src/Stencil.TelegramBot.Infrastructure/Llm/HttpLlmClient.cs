using System.Net;
using System.Net.Http.Headers;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Text.Json.Nodes;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Serialization;

namespace Stencil.TelegramBot.Infrastructure.Llm;

// The transport every §6 provider shares — POST, bearer, status/JSON handling; the wire shapes live
// one per IProviderMapping. Non-2xx, unreachable and unparseable become LlmExceptions;
// truncation/refusal are typed.
public sealed class HttpLlmClient : ILlmClient
{
    // LLM calls are slow: the canonical providers.json timeouts.chatSeconds.
    public static readonly TimeSpan DefaultTimeout = TimeSpan.FromSeconds(ProvidersAsset.ChatTimeoutSeconds);

    // The §6 table; a new provider is an entry plus a file.
    private static readonly IReadOnlyDictionary<string, IProviderMapping> _mappings =
        new Dictionary<string, IProviderMapping>(StringComparer.Ordinal)
        {
            [LlmOptions.PROVIDER_OLLAMA] = new OllamaMapping(),
            [LlmOptions.PROVIDER_OPEN_AI_COMPAT] = new OpenAiMapping(),
            [LlmOptions.PROVIDER_STENCIL_SERVER] = new StencilServerMapping(),
        };

    private readonly HttpClient _http;
    private readonly LlmOptions _options;

    public HttpLlmClient(HttpClient http, LlmOptions options)
    {
        _http = http;
        _options = options;
    }

    public async Task<LlmReply> ChatAsync(LlmChatRequest request, CancellationToken ct = default)
    {
        LlmOptions options = request.Options ?? _options;
        if (!_mappings.TryGetValue(options.Provider, out IProviderMapping? mapping))
        {
            throw LlmException.Deployment(
                "The AI assistant isn't configured on this bot.",
                $"unknown LLM provider \"{options.Provider}\" — set STENCIL_LLM_PROVIDER to "
                + "ollama, openai-compat or stencil-server");
        }
        using JsonDocument doc = await postAsync(
            mapping.Url(request, options), mapping.Body(request, options),
            mapping.Bearer(request, options), ct).ConfigureAwait(false);
        return mapping.Read(doc.RootElement);
    }

    private async Task<JsonDocument> postAsync(string url, JsonObject body, string? bearer, CancellationToken ct)
    {
        // Streamed straight onto the request: with multi-MB base64 images a ToJsonString() copy
        // would land on the LOH.
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
                throw errorFor((int)response.StatusCode, bytes);
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

    // The reason is said ONCE (§6.3): a provider that explains itself is quoted as-is, no status
    // restating it.
    private static LlmException errorFor(int status, byte[] body)
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
            }
        }
        return new LlmException(
            detail.Length == 0 ? $"The LLM endpoint answered HTTP {status}." : detail,
            disabled ? LlmFailure.Disabled : LlmFailure.Error);
    }

    public const int MAX_PROVIDER_DETAIL = 200;

    private static readonly Regex _controlish = new(@"[\p{Cc}\p{Cf}]", RegexOptions.Compiled);
    private static readonly Regex _urlish = new(@"[a-z][a-z0-9+.-]*://\S+", RegexOptions.Compiled | RegexOptions.IgnoreCase);
    private static readonly Regex _secretish = new(
        @"(?:bearer|basic) +[A-Za-z0-9._~+/=-]{8,}"
        + @"|\b(?:sk|pk|api[-_]?key|key|token|secret)[-_=:][A-Za-z0-9._-]{6,}"
        + @"|[A-Za-z0-9_-]{24,}",
        RegexOptions.Compiled | RegexOptions.IgnoreCase);

    // Control characters out, URLs and token-shaped runs redacted (an endpoint may echo the key);
    // port of the server's sanitizeUpstreamText.
    /// </summary>
    public static string SanitizeProviderText(string text)
    {
        if (text.Length == 0)
        {
            return "";
        }
        string t = text.Length > 4 * MAX_PROVIDER_DETAIL ? text[..(4 * MAX_PROVIDER_DETAIL)] : text;
        t = _controlish.Replace(t, " ");
        t = _secretish.Replace(_urlish.Replace(t, "[redacted]"), "[redacted]");
        t = string.Join(' ', t.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries));
        return t.Length <= MAX_PROVIDER_DETAIL ? t : t[..(MAX_PROVIDER_DETAIL - 1)].TrimEnd() + "…";
    }

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
