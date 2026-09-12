using System.Net.Http.Headers;
using System.Text;
using System.Text.Json;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Serialization;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Infrastructure.Server;

// A port of pystencil's ServerConnection: REST only, every non-2xx a ServerException with the
// server's {code, message}. The HttpClient is the caller's (the factory wires TLS); all JSON goes
// through StencilJson.
public sealed partial class HttpStencilServerClient : IStencilServerClient
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

    public string BaseUrl { get; }

    // Handshake (pystencil connect / browser handshake() parity): no token mints one, a token is
    // validated by listing projects, and a 401/403 there may be the ADMIN token — SendAsync
    // re-mints and the session token is adopted.
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
            // The credential outlives server restarts: SendAsync re-mints with it when the session
            // token goes stale.
            _credential = _token;
            // A proven admin token cannot list projects, so mint straight away instead of a probe
            // that always 401s.
            if (_kind == CredentialKind.Admin && await TryMintAsync(ct).ConfigureAwait(false) is string minted)
            {
                _token = minted;
            }
            await ListProjectsAsync(ct).ConfigureAwait(false);
            // SendAsync's rescue round promotes the kind; a credential that listed directly is a
            // session token.
            if (_kind != CredentialKind.Admin)
            {
                _kind = CredentialKind.Session;
            }
        }
        return new ServerHandshake(_token, _kind);
    }

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

    public async Task<ProjectRecord> CreateProjectAsync(CreateProjectRequest request, CancellationToken ct = default)
    {
        string json = StencilJson.Serialize(request);
        using JsonDocument doc = await SendJsonAsync(HttpMethod.Post, "/projects", JsonContent(json), ct)
            .ConfigureAwait(false);
        return doc.RootElement.Deserialize<ProjectRecord>(StencilJson.Options) ?? new ProjectRecord();
    }

    // 409 ⇒ conflict.
    public async Task<ProjectRecord> UpdateProjectAsync(string id, UpdateProjectRequest request, CancellationToken ct = default)
    {
        string json = StencilJson.Serialize(request);
        using JsonDocument doc = await SendJsonAsync(HttpMethod.Put, ProjectPath(id), JsonContent(json), ct)
            .ConfigureAwait(false);
        return doc.RootElement.Deserialize<ProjectRecord>(StencilJson.Options) ?? new ProjectRecord();
    }

    public async Task DeleteProjectAsync(string id, CancellationToken ct = default)
    {
        using HttpResponseMessage response = await SendAsync(HttpMethod.Delete, ProjectPath(id), null, ct)
            .ConfigureAwait(false);
        await EnsureSuccessAsync(response, ct).ConfigureAwait(false);
    }

    public async Task<byte[]> GetFileAsync(string id, string kind, CancellationToken ct = default)
    {
        string path = FilePath(id, kind);
        using HttpResponseMessage response = await SendAsync(HttpMethod.Get, path, null, ct)
            .ConfigureAwait(false);
        await EnsureSuccessAsync(response, ct).ConfigureAwait(false);
        return await response.Content.ReadAsByteArrayAsync(ct).ConfigureAwait(false);
    }

    // The server is codec-free: dimensions and the extension hint ride the query, the pixel bytes
    // the body.
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

    // Idempotent, filestore-only kinds (§9); never bumps the project version.
    public async Task DeleteFileAsync(string id, string kind, CancellationToken ct = default)
    {
        using HttpResponseMessage response = await SendAsync(HttpMethod.Delete, FilePath(id, kind), null, ct)
            .ConfigureAwait(false);
        await EnsureSuccessAsync(response, ct).ConfigureAwait(false);
    }
}
