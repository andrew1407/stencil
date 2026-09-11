using System.Net.Http.Headers;
using System.Text;
using System.Text.Json;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Serialization;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Infrastructure.Server;

/// <summary>
/// A REST client for one Stencil collaboration server — a faithful port of
/// <c>pystencil</c>'s <c>ServerConnection</c> (and the browser net layer it ports). REST only:
/// no live <c>/ws</c> feed. Every non-2xx surfaces as a <see cref="ServerException"/> carrying
/// the server's structured <c>{code, message}</c> plus the HTTP status.
/// </summary>
/// <remarks>
/// The <see cref="HttpClient"/> is supplied by the caller (the factory wires TLS verification),
/// so this type never constructs one — tests inject a stub message handler. All (de)serialisation
/// goes through <see cref="StencilJson"/> so the camelCase wire shapes match every other
/// front-end.
/// </remarks>
public sealed class HttpStencilServerClient : IStencilServerClient
{
    private readonly HttpClient _http;
    private string _token;
    private string _credential;
    private CredentialKind _kind;

    public HttpStencilServerClient(HttpClient http, string baseUrl, string? token, string? credential = null,
        CredentialKind credentialKind = CredentialKind.None)
    {
        _http = http;
        BaseUrl = UrlNormalizer.Normalize(baseUrl);
        _token = token ?? "";
        _credential = credential ?? "";
        _kind = credentialKind;
    }

    /// <summary>The normalised origin this client talks to (<c>scheme://host[:port]</c>).</summary>
    public string BaseUrl { get; }

    /// <summary>
    /// Acquire or validate a token (handshake, mirroring <c>pystencil</c> <c>connect</c> + the
    /// extension's <c>connect</c>): with no token, mint one via <c>POST /auth/token</c>; with a
    /// token, validate it by listing projects. When that probe 401/403s the value may be the
    /// server's ADMIN token — <see cref="SendAsync"/> re-mints with it as bearer and retries, so
    /// the minted session token is adopted; a failed mint surfaces the probe's own rejection.
    /// The effective token is stored on this client and returned with the credential kind the
    /// handshake proved (browser <c>handshake()</c> parity).
    /// </summary>
    public async Task<ServerHandshake> ConnectAsync(string? token, CancellationToken ct = default)
    {
        if (token is not null)
        {
            _token = token;
        }
        if (string.IsNullOrEmpty(_token))
        {
            using JsonDocument doc = await SendJsonAsync(HttpMethod.Post, "/auth/token", EmptyBody(), ct)
                .ConfigureAwait(false);
            _token = JsonRead.ReadString(doc.RootElement, "token");
            _kind = CredentialKind.None; // minted anonymously: there is no credential to classify
        }
        else
        {
            // The credential is what the user supplied — it outlives server restarts
            // (SendAsync re-mints with it when a stored session token goes stale).
            _credential = _token;
            // A credential already proven to be an admin token cannot list projects, so mint
            // straight away instead of spending a probe that always 401s.
            if (_kind == CredentialKind.Admin && await TryMintAsync(ct).ConfigureAwait(false) is string minted)
            {
                _token = minted;
            }
            await ListProjectsAsync(ct).ConfigureAwait(false);
            // SendAsync's rescue round promotes the kind when the probe had to be re-minted;
            // a credential that listed directly is simply a session token.
            if (_kind != CredentialKind.Admin)
            {
                _kind = CredentialKind.Session;
            }
        }
        return new ServerHandshake(_token, _kind);
    }

    /// <summary><c>GET /projects</c> → the project records.</summary>
    public async Task<IReadOnlyList<ProjectRecord>> ListProjectsAsync(CancellationToken ct = default)
    {
        using JsonDocument doc = await SendJsonAsync(HttpMethod.Get, "/projects", null, ct)
            .ConfigureAwait(false);
        if (!doc.RootElement.TryGetProperty("projects", out JsonElement projects)
            || projects.ValueKind != JsonValueKind.Array)
        {
            return Array.Empty<ProjectRecord>();
        }
        List<ProjectRecord> result = new();
        foreach (JsonElement element in projects.EnumerateArray())
        {
            ProjectRecord? record = element.Deserialize<ProjectRecord>(StencilJson.Options);
            if (record is not null)
            {
                result.Add(record);
            }
        }
        return result;
    }

    /// <summary><c>GET /projects/{id}</c> → the project plus its layout and original payload.</summary>
    public async Task<ProjectFull> GetProjectAsync(string id, CancellationToken ct = default)
    {
        using JsonDocument doc = await SendJsonAsync(HttpMethod.Get, ProjectPath(id), null, ct).ConfigureAwait(false);
        JsonElement root = doc.RootElement;
        ProjectRecord project = root.TryGetProperty("project", out JsonElement projectElement)
            ? projectElement.Deserialize<ProjectRecord>(StencilJson.Options) ?? new ProjectRecord()
            : new ProjectRecord();
        JsonElement? layout = root.TryGetProperty("layout", out JsonElement layoutElement)
            ? layoutElement.Clone()
            : null;
        string? originalContent = root.TryGetProperty("originalContent", out JsonElement contentElement)
            && contentElement.ValueKind == JsonValueKind.String
            ? contentElement.GetString()
            : null;
        return new ProjectFull
        {
            Project = project,
            Layout = layout,
            OriginalContent = originalContent,
        };
    }

    /// <summary><c>POST /projects</c> (null fields dropped) → the created record.</summary>
    public async Task<ProjectRecord> CreateProjectAsync(CreateProjectRequest request, CancellationToken ct = default)
    {
        string json = StencilJson.Serialize(request);
        using JsonDocument doc = await SendJsonAsync(HttpMethod.Post, "/projects", JsonContent(json), ct)
            .ConfigureAwait(false);
        return doc.RootElement.Deserialize<ProjectRecord>(StencilJson.Options) ?? new ProjectRecord();
    }

    /// <summary><c>PUT /projects/{id}</c> → the updated record (409 ⇒ conflict).</summary>
    public async Task<ProjectRecord> UpdateProjectAsync(string id, UpdateProjectRequest request, CancellationToken ct = default)
    {
        string json = StencilJson.Serialize(request);
        using JsonDocument doc = await SendJsonAsync(HttpMethod.Put, ProjectPath(id), JsonContent(json), ct)
            .ConfigureAwait(false);
        return doc.RootElement.Deserialize<ProjectRecord>(StencilJson.Options) ?? new ProjectRecord();
    }

    /// <summary><c>DELETE /projects/{id}</c> (204 No Content).</summary>
    public async Task DeleteProjectAsync(string id, CancellationToken ct = default)
    {
        using HttpResponseMessage response = await SendAsync(HttpMethod.Delete, ProjectPath(id), null, ct)
            .ConfigureAwait(false);
        await EnsureSuccessAsync(response, ct).ConfigureAwait(false);
    }

    /// <summary><c>GET /projects/{id}/files/{kind}</c> → raw image bytes.</summary>
    public async Task<byte[]> GetFileAsync(string id, string kind, CancellationToken ct = default)
    {
        string path = FilePath(id, kind);
        using HttpResponseMessage response = await SendAsync(HttpMethod.Get, path, null, ct)
            .ConfigureAwait(false);
        await EnsureSuccessAsync(response, ct).ConfigureAwait(false);
        return await response.Content.ReadAsByteArrayAsync(ct).ConfigureAwait(false);
    }

    /// <summary>
    /// <c>POST /projects/{id}/files/{kind}?ext&amp;w&amp;h</c> with an octet-stream body →
    /// the stored path/dimensions. The server is codec-free, so the dimensions and extension
    /// hint ride the query while the pixel bytes go in the body.
    /// </summary>
    public async Task<FileWriteResult> PutFileAsync(string id, string kind, byte[] data, string ext, int w, int h, CancellationToken ct = default)
    {
        string path = $"{FilePath(id, kind)}?ext={Uri.EscapeDataString(ext)}&w={w}&h={h}";
        ByteArrayContent content = new(data);
        content.Headers.ContentType = new MediaTypeHeaderValue("application/octet-stream");
        using JsonDocument doc = await SendJsonAsync(HttpMethod.Post, path, content, ct).ConfigureAwait(false);
        JsonElement root = doc.RootElement;
        string storedPath = JsonRead.ReadString(root, "path");
        int width = JsonRead.ReadInt(root, "w");
        int height = JsonRead.ReadInt(root, "h");
        return new FileWriteResult(storedPath, width, height);
    }

    /// <summary>
    /// <c>DELETE /projects/{id}/files/{kind}</c> (204 No Content — idempotent, filestore-only
    /// kinds per contract §9; never bumps the project version).
    /// </summary>
    public async Task DeleteFileAsync(string id, string kind, CancellationToken ct = default)
    {
        using HttpResponseMessage response = await SendAsync(HttpMethod.Delete, FilePath(id, kind), null, ct)
            .ConfigureAwait(false);
        await EnsureSuccessAsync(response, ct).ConfigureAwait(false);
    }

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
