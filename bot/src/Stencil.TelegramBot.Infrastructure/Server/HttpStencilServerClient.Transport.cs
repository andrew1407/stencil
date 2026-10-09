using System.Net.Http.Headers;
using System.Text;
using System.Text.Json;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Serialization;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Net;

namespace Stencil.TelegramBot.Infrastructure.Server;

public sealed partial class HttpStencilServerClient
{
    // A token or an error reply is a few hundred bytes; anything past this is not one.
    private const long _smallBodyBytes = 64 * 1024;

    private async Task<JsonDocument> sendJsonAsync(HttpMethod method, string path, HttpContent? content, CancellationToken ct) =>
        parse(await sendBytesAsync(method, path, content, ct).ConfigureAwait(false));

    private async Task<byte[]> sendBytesAsync(HttpMethod method, string path, HttpContent? content, CancellationToken ct)
    {
        using HttpResponseMessage response = await sendAsync(method, path, content, ct).ConfigureAwait(false);
        await ensureSuccessAsync(response, ct).ConfigureAwait(false);
        return await readBodyAsync(response, ct).ConfigureAwait(false);
    }

    private static JsonDocument parse(byte[] body) => JsonDocument.Parse(body.Length == 0 ? "{}"u8.ToArray() : body);

    // A stored session token dies with a server DB wipe: on 401/403, when this client carries its credential,
    // re-mint once and retry in place (extension connections.js req parity); /auth/token never retries.
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

    private async Task<HttpResponseMessage> sendOnceAsync(HttpMethod method, string path, HttpContent? content, string bearer, CancellationToken ct)
    {
        HttpRequestMessage request = new(method, BaseUrl + path);
        request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", bearer);
        if (content is not null)
        {
            request.Content = content;
        }
        try
        {
            return await _http.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, ct).ConfigureAwait(false);
        }
        catch (HttpRequestException ex) when (GuardedConnect.RefusalIn(ex) is InvalidOperationException refused)
        {
            throw refused;
        }
        catch (HttpRequestException ex)
        {
            throw new ServerException("unreachable", $"couldn't reach {BaseUrl} ({ex.Message})");
        }
        // HttpClient.Timeout surfaces as a cancellation the caller never asked for.
        catch (TaskCanceledException) when (!ct.IsCancellationRequested)
        {
            throw new ServerException("timeout", $"{BaseUrl} didn't answer within {_http.Timeout.TotalSeconds:0} s");
        }
    }

    private async Task<byte[]> readBodyAsync(HttpResponseMessage response, CancellationToken ct) =>
        await CappedBody.ReadAsync(response.Content, MaxResponseBytes, ct).ConfigureAwait(false)
            ?? throw new ServerException(
                "tooLarge", $"the server's reply is over the {MaxResponseBytes / (1024 * 1024)} MB limit");

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
            byte[]? body = await CappedBody.ReadAsync(response.Content, _smallBodyBytes, ct).ConfigureAwait(false);
            if (body is null)
            {
                return null;
            }
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
            byte[] body = await CappedBody.ReadAsync(response.Content, _smallBodyBytes, ct).ConfigureAwait(false) ?? [];
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
