using System.Net;
using System.Net.Http.Headers;
using System.Text.Json;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Serialization;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Configuration;

namespace Stencil.TelegramBot.Infrastructure.Server;

// A port of pystencil's ServerConnection: REST only, every non-2xx a ServerException with the server's
// {code, message}. The HttpClient is the caller's (the factory wires TLS); all JSON goes through StencilJson.
public sealed partial class HttpStencilServerClient : IStencilServerClient
{
    private readonly HttpClient _http;
    private string _token;
    private string _credential;
    private CredentialKind _kind;

    public HttpStencilServerClient(HttpClient http, string baseUrl, string? token, string? credential = null,
        CredentialKind credentialKind = CredentialKind.NONE)
    {
        _http = http;
        BaseUrl = UrlNormalizer.Normalize(baseUrl);
        _token = token ?? "";
        _credential = credential ?? "";
        _kind = credentialKind;
    }

    public string BaseUrl { get; }

    public long MaxResponseBytes { get; init; } = BotOptions.DEFAULT_MAX_SERVER_RESPONSE_BYTES;

    public int ProjectListLimit { get; init; } = BotOptions.DEFAULT_PROJECT_LIST_LIMIT;

    // The CLI's max_pages: a server that keeps handing out cursors is cut off, never followed forever.
    public const int MAX_LIST_PAGES = 1000;

    // Handshake (pystencil connect / browser handshake() parity): no token mints one, a token is validated by
    // the session probe, and a 401/403 there may be the ADMIN token — SendAsync re-mints.
    public async Task<ServerHandshake> ConnectAsync(string? token, CancellationToken ct = default)
    {
        if (token is not null)
        {
            _token = token;
        }
        if (string.IsNullOrEmpty(_token))
        {
            using JsonDocument doc = await sendJsonAsync(HttpMethod.Post, "/auth/token", emptyBody(), ct)
                .ConfigureAwait(false);
            _token = JsonRead.ReadString(doc.RootElement, "token");
            _kind = CredentialKind.NONE; // minted anonymously: there is no credential to classify
        }
        else
        {
            // The credential outlives server restarts: SendAsync re-mints with it when the session
            // token goes stale.
            _credential = _token;
            // A proven admin token cannot list projects, so mint straight away instead of a probe
            // that always 401s.
            if (_kind == CredentialKind.ADMIN && await tryMintAsync(ct).ConfigureAwait(false) is string minted)
            {
                _token = minted;
            }
            await probeSessionAsync(ct).ConfigureAwait(false);
            // SendAsync's rescue round promotes the kind; a credential that listed directly is a
            // session token.
            if (_kind != CredentialKind.ADMIN)
            {
                _kind = CredentialKind.SESSION;
            }
        }
        return new ServerHandshake(_token, _kind);
    }

    // GET /auth/session is the cheap check; a server older than it 404s there and gets a one-row page.
    private async Task probeSessionAsync(CancellationToken ct)
    {
        using HttpResponseMessage session = await sendAsync(HttpMethod.Get, "/auth/session", null, ct)
            .ConfigureAwait(false);
        if (session.StatusCode != HttpStatusCode.NotFound)
        {
            await ensureSuccessAsync(session, ct).ConfigureAwait(false);
            return;
        }
        using HttpResponseMessage page = await sendAsync(HttpMethod.Get, "/projects?limit=1", null, ct)
            .ConfigureAwait(false);
        await ensureSuccessAsync(page, ct).ConfigureAwait(false);
    }

    // Every page, following nextCursor: ProjectListLimit sizes a page and never caps the list.
    public async Task<IReadOnlyList<ProjectRecord>> ListProjectsAsync(CancellationToken ct = default)
    {
        List<ProjectRecord> result = new();
        HashSet<string> seen = new(StringComparer.Ordinal);
        string path = $"/projects?limit={ProjectListLimit}";
        for (int page = 0; page < MAX_LIST_PAGES; page++)
        {
            using JsonDocument doc = await sendJsonAsync(HttpMethod.Get, path, null, ct).ConfigureAwait(false);
            addProjects(doc.RootElement, result);
            string cursor = JsonRead.ReadString(doc.RootElement, "nextCursor");
            if (cursor.Length == 0)
            {
                return result;
            }
            if (!seen.Add(cursor))
            {
                throw new ServerException("badResponse", "the server handed back the same page cursor twice");
            }
            path = $"/projects?limit={ProjectListLimit}&after={Uri.EscapeDataString(cursor)}";
        }
        throw new ServerException("badResponse", $"the server kept paging past {MAX_LIST_PAGES} pages");
    }

    private static void addProjects(JsonElement root, List<ProjectRecord> result)
    {
        if (!root.TryGetProperty("projects", out JsonElement projects) || projects.ValueKind != JsonValueKind.Array)
        {
            return;
        }
        foreach (JsonElement element in projects.EnumerateArray())
        {
            ProjectRecord? record = element.Deserialize<ProjectRecord>(StencilJson.Options);
            if (record is not null)
            {
                result.Add(record);
            }
        }
    }

    public async Task<ProjectFull> GetProjectAsync(string id, CancellationToken ct = default)
    {
        using JsonDocument doc = await sendJsonAsync(HttpMethod.Get, projectPath(id), null, ct).ConfigureAwait(false);
        JsonElement root = doc.RootElement;
        ProjectRecord project = root.TryGetProperty("project", out JsonElement projectElement)
            ? projectElement.Deserialize<ProjectRecord>(StencilJson.Options) ?? new ProjectRecord()
            : new ProjectRecord();
        JsonElement? layout = root.TryGetProperty("layout", out JsonElement layoutElement)
            ? layoutElement.Clone()
            : null;
        return new ProjectFull
        {
            Project = project,
            Layout = layout,
        };
    }

    public async Task<ProjectRecord> CreateProjectAsync(CreateProjectRequest request, CancellationToken ct = default)
    {
        string json = StencilJson.Serialize(request);
        using JsonDocument doc = await sendJsonAsync(HttpMethod.Post, "/projects", jsonContent(json), ct)
            .ConfigureAwait(false);
        return doc.RootElement.Deserialize<ProjectRecord>(StencilJson.Options) ?? new ProjectRecord();
    }

    // 409 ⇒ conflict.
    public async Task<ProjectRecord> UpdateProjectAsync(string id, UpdateProjectRequest request, CancellationToken ct = default)
    {
        string json = StencilJson.Serialize(request);
        using JsonDocument doc = await sendJsonAsync(HttpMethod.Put, projectPath(id), jsonContent(json), ct)
            .ConfigureAwait(false);
        return doc.RootElement.Deserialize<ProjectRecord>(StencilJson.Options) ?? new ProjectRecord();
    }

    public async Task DeleteProjectAsync(string id, CancellationToken ct = default)
    {
        using HttpResponseMessage response = await sendAsync(HttpMethod.Delete, projectPath(id), null, ct)
            .ConfigureAwait(false);
        await ensureSuccessAsync(response, ct).ConfigureAwait(false);
    }

    public async Task<byte[]> GetFileAsync(string id, string kind, CancellationToken ct = default)
    {
        string path = filePath(id, kind);
        using HttpResponseMessage response = await sendAsync(HttpMethod.Get, path, null, ct)
            .ConfigureAwait(false);
        await ensureSuccessAsync(response, ct).ConfigureAwait(false);
        return await readBodyAsync(response, ct).ConfigureAwait(false);
    }

    // The server is codec-free: dimensions and the extension hint ride the query, the pixel bytes
    // the body.
    public async Task<FileWriteResult> PutFileAsync(string id, string kind, byte[] data, string ext, int w, int h, CancellationToken ct = default)
    {
        string path = $"{filePath(id, kind)}?ext={Uri.EscapeDataString(ext)}&w={w}&h={h}";
        ByteArrayContent content = new(data);
        content.Headers.ContentType = new MediaTypeHeaderValue("application/octet-stream");
        using JsonDocument doc = await sendJsonAsync(HttpMethod.Post, path, content, ct).ConfigureAwait(false);
        JsonElement root = doc.RootElement;
        string storedPath = JsonRead.ReadString(root, "path");
        int width = JsonRead.ReadInt(root, "w");
        int height = JsonRead.ReadInt(root, "h");
        return new FileWriteResult(storedPath, width, height);
    }

    // Idempotent, filestore-only kinds (§9); never bumps the project version.
    public async Task DeleteFileAsync(string id, string kind, CancellationToken ct = default)
    {
        using HttpResponseMessage response = await sendAsync(HttpMethod.Delete, filePath(id, kind), null, ct)
            .ConfigureAwait(false);
        await ensureSuccessAsync(response, ct).ConfigureAwait(false);
    }
}
