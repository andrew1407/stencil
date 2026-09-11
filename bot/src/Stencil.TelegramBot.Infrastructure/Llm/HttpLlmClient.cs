using System.Net;
using System.Net.Http.Headers;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Text.Json.Nodes;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Serialization;

namespace Stencil.TelegramBot.Infrastructure.Llm;

/// <summary>
/// The provider adapter for the three wire mappings of <c>llm-contract.md</c> §6. The shapes
/// themselves live one per <see cref="IProviderMapping"/>; this class is the transport —
/// POST, bearer, status/JSON handling — that every provider shares.
/// </summary>
/// <remarks>
/// The <see cref="HttpClient"/> is the caller's, so tests inject a stub handler. Non-2xx
/// responses, unreachable endpoints and unparseable payloads become
/// <see cref="LlmException"/>s, and a truncated or refused reply is typed rather than parsed
/// as a plan.
/// </remarks>
public sealed class HttpLlmClient : ILlmClient
{
    /// <summary>LLM calls are slow — the canonical <c>providers.json</c> <c>timeouts.chatSeconds</c>.</summary>
    public static readonly TimeSpan DefaultTimeout = TimeSpan.FromSeconds(ProvidersAsset.ChatTimeoutSeconds);

    /// <summary>The §6 table: one mapping per provider id. A new provider is an entry plus a file.</summary>
    private static readonly IReadOnlyDictionary<string, IProviderMapping> Mappings =
        new Dictionary<string, IProviderMapping>(StringComparer.Ordinal)
        {
            [LlmOptions.ProviderOllama] = new OllamaMapping(),
            [LlmOptions.ProviderOpenAiCompat] = new OpenAiMapping(),
            [LlmOptions.ProviderStencilServer] = new StencilServerMapping(),
        };

    private readonly HttpClient _http;
    private readonly LlmOptions _options;

    public HttpLlmClient(HttpClient http, LlmOptions options)
    {
        _http = http;
        _options = options;
    }

    /// <inheritdoc />
    public async Task<LlmReply> ChatAsync(LlmChatRequest request, CancellationToken ct = default)
    {
        // The call's own config when the user picked a profile (/chatapi), else the operator's.
        LlmOptions options = request.Options ?? _options;
        if (!Mappings.TryGetValue(options.Provider, out IProviderMapping? mapping))
        {
            throw LlmException.Deployment(
                "The AI assistant isn't configured on this bot.",
                $"unknown LLM provider \"{options.Provider}\" — set STENCIL_LLM_PROVIDER to "
                + "ollama, openai-compat or stencil-server");
        }
        using JsonDocument doc = await PostAsync(
            mapping.Url(request, options), mapping.Body(request, options),
            mapping.Bearer(request, options), ct).ConfigureAwait(false);
        return mapping.Read(doc.RootElement);
    }

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
    /// The typed failure for a non-2xx response: the error body's own line (the shared
    /// <see cref="JsonRead.ErrorDetail"/> shapes), else the bare status; <c>llmDisabled</c>
    /// types as <see cref="LlmFailure.Disabled"/>. The reason is said ONCE (§6.3) — a provider
    /// that explains itself is quoted as-is, with no preamble and no status restating it.
    /// </summary>
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
    /// Untrusted provider prose made safe for a chat: control characters out, URLs and
    /// token-shaped runs redacted (an endpoint may echo the key back), whitespace collapsed,
    /// hard-truncated. Port of the server's <c>sanitizeUpstreamText</c>.
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

    /// <summary>An <c>application/json</c> body streamed straight to the request via
    /// <see cref="Utf8JsonWriter"/> — no intermediate string.</summary>
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
