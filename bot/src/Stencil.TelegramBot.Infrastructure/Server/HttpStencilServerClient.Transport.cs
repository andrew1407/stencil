using System.Net.Http.Headers;
using System.Text;
using System.Text.Json;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Serialization;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Infrastructure.Server;

public sealed partial class HttpStencilServerClient
{
    private async Task<JsonDocument> sendJsonAsync(HttpMethod method, string path, HttpContent? content, CancellationToken ct)
    {
        using HttpResponseMessage response = await sendAsync(method, path, content, ct).ConfigureAwait(false);
        await ensureSuccessAsync(response, ct).ConfigureAwait(false);
        byte[] body = await response.Content.ReadAsByteArrayAsync(ct).ConfigureAwait(false);
        if (body.Length == 0)
        {
            return JsonDocument.Parse("{}");
        }
        return JsonDocument.Parse(body);
    }

    // A stored session token dies with a server DB wipe: on 401/403, when this client carries its
    // credential, re-mint once and retry in place (extension connections.js req parity);
    // /auth/token itself never retries.
    private async Task<HttpResponseMessage> sendAsync(HttpMethod method, string path, HttpContent? content, CancellationToken ct)
    {
        HttpResponseMessage response = await sendOnceAsync(method, path, content, _token, ct).ConfigureAwait(false);
        int status = (int)response.StatusCode;
        if ((status == 401 || status == 403) && _credential.Length != 0 && path != "/auth/token")
        {
            string? minted = await tryMintAsync(ct).ConfigureAwait(false);
            if (minted is not null)
            {
                response.Dispose();
                _token = minted;
                response = await sendOnceAsync(method, path, content, _token, ct).ConfigureAwait(false);
                if (response.IsSuccessStatusCode)
                {
                    // It minted AND the session works: this credential is an admin token.
                    _kind = CredentialKind.ADMIN;
                }
            }
        }
        return response;
    }

    private Task<HttpResponseMessage> sendOnceAsync(HttpMethod method, string path, HttpContent? content, string bearer, CancellationToken ct)
    {
        HttpRequestMessage request = new(method, BaseUrl + path);
        request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", bearer);
        if (content is not null)
        {
            request.Content = content;
        }
        return _http.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, ct);
    }

    // Null on any refusal.
    private async Task<string?> tryMintAsync(CancellationToken ct)
    {
        using HttpResponseMessage response = await sendOnceAsync(HttpMethod.Post, "/auth/token", emptyBody(), _credential, ct)
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

    private static async Task ensureSuccessAsync(HttpResponseMessage response, CancellationToken ct)
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
        }
        throw new ServerException(code, message, status);
    }

    private static string projectPath(string id) => "/projects/" + Uri.EscapeDataString(id);

    private static string filePath(string id, string kind) =>
        projectPath(id) + "/files/" + Uri.EscapeDataString(kind);

    private static StringContent emptyBody() => jsonContent("{}");

    private static StringContent jsonContent(string json) =>
        new(json, Encoding.UTF8, "application/json");
}
