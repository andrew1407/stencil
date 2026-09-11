using System.Net.Http.Headers;
using System.Text;
using System.Text.Json;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Serialization;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Infrastructure.Server;

// HttpStencilServerClient — the wire itself: bearer handling with a one-shot re-mint on 401,
// the {code, message} error lift and the path/body helpers. Class doc lives in
// HttpStencilServerClient.cs.
public sealed partial class HttpStencilServerClient
{
    private async Task<JsonDocument> SendJsonAsync(HttpMethod method, string path, HttpContent? content, CancellationToken ct)
    {
        using HttpResponseMessage response = await SendAsync(method, path, content, ct).ConfigureAwait(false);
        await EnsureSuccessAsync(response, ct).ConfigureAwait(false);
        byte[] body = await response.Content.ReadAsByteArrayAsync(ct).ConfigureAwait(false);
        if (body.Length == 0)
        {
            return JsonDocument.Parse("{}");
        }
        return JsonDocument.Parse(body);
    }

    /// <summary>
    /// Issue one request with the bearer header attached. A stored session token dies with a
    /// server DB wipe — on a 401/403, when this client carries its original credential, re-mint
    /// once with it and retry in place (extension <c>connections.js</c> <c>req</c> parity). A
    /// failed mint keeps the original rejection, and <c>/auth/token</c> itself never retries.
    /// </summary>
    private async Task<HttpResponseMessage> SendAsync(HttpMethod method, string path, HttpContent? content, CancellationToken ct)
    {
        HttpResponseMessage response = await SendOnceAsync(method, path, content, _token, ct).ConfigureAwait(false);
        int status = (int)response.StatusCode;
        if ((status == 401 || status == 403) && _credential.Length != 0 && path != "/auth/token")
        {
            string? minted = await TryMintAsync(ct).ConfigureAwait(false);
            if (minted is not null)
            {
                response.Dispose();
                _token = minted;
                response = await SendOnceAsync(method, path, content, _token, ct).ConfigureAwait(false);
                if (response.IsSuccessStatusCode)
                {
                    // It minted AND the session works: this credential is an admin token.
                    _kind = CredentialKind.Admin;
                }
            }
        }
        return response;
    }

    /// <summary>One request on the wire with the given bearer — no retry logic.</summary>
    private Task<HttpResponseMessage> SendOnceAsync(HttpMethod method, string path, HttpContent? content, string bearer, CancellationToken ct)
    {
        HttpRequestMessage request = new(method, BaseUrl + path);
        request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", bearer);
        if (content is not null)
        {
            request.Content = content;
        }
        return _http.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, ct);
    }

    /// <summary><c>POST /auth/token</c> with the credential as bearer; null on any refusal.</summary>
    private async Task<string?> TryMintAsync(CancellationToken ct)
    {
        using HttpResponseMessage response = await SendOnceAsync(HttpMethod.Post, "/auth/token", EmptyBody(), _credential, ct)
            .ConfigureAwait(false);
        if (!response.IsSuccessStatusCode)
        {
            return null;
        }
        try
        {
            byte[] body = await response.Content.ReadAsByteArrayAsync(ct).ConfigureAwait(false);
            using JsonDocument doc = JsonDocument.Parse(body);
            string token = JsonRead.ReadString(doc.RootElement, "token");
            return token.Length == 0 ? null : token;
        }
        catch (JsonException)
        {
            return null;
        }
    }

    private static async Task EnsureSuccessAsync(HttpResponseMessage response, CancellationToken ct)
    {
        if (response.IsSuccessStatusCode)
        {
            return;
        }
        int status = (int)response.StatusCode;
        string code = "";
        string message = $"HTTP {status}";
        try
        {
            byte[] body = await response.Content.ReadAsByteArrayAsync(ct).ConfigureAwait(false);
            if (body.Length != 0)
            {
                using JsonDocument doc = JsonDocument.Parse(body);
                if (doc.RootElement.ValueKind == JsonValueKind.Object)
                {
                    code = JsonRead.ReadString(doc.RootElement, "code");
                    string parsed = JsonRead.ReadString(doc.RootElement, "message");
                    if (parsed.Length != 0)
                    {
                        message = parsed;
                    }
                }
            }
        }
        catch (JsonException)
        {
            // Non-JSON error body — keep the generic "HTTP <status>" message.
        }
        throw new ServerException(code, message, status);
    }

    /// <summary>The <c>/projects/{id}</c> path with the id escaped.</summary>
    private static string ProjectPath(string id) => "/projects/" + Uri.EscapeDataString(id);

    /// <summary>The <c>/projects/{id}/files/{kind}</c> path with both segments escaped.</summary>
    private static string FilePath(string id, string kind) =>
        ProjectPath(id) + "/files/" + Uri.EscapeDataString(kind);

    /// <summary>Empty JSON object body for <c>POST /auth/token</c>.</summary>
    private static StringContent EmptyBody() => JsonContent("{}");

    private static StringContent JsonContent(string json) =>
        new(json, Encoding.UTF8, "application/json");
}
