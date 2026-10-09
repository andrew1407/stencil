using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Domain.Abstractions;

// A port of pystencil's ServerConnection: REST only; every non-2xx surfaces as ServerException.
public interface IStencilServerClient
{
    string BaseUrl { get; }

    // The token this client holds now and its credential's kind; a re-mint after a 401 changes both.
    ServerHandshake Session { get; }

    // No token: mint one via POST /auth/token. A token the session probe refuses may be the server's
    // ADMIN token: mint a session token with it as bearer and adopt that instead.
    Task<ServerHandshake> ConnectAsync(string? token, CancellationToken ct = default);

    // GET /projects, newest-updated first, every page.
    Task<IReadOnlyList<ProjectRecord>> ListProjectsAsync(CancellationToken ct = default);

    // GET /projects, the newest-updated `limit` only: one page, never followed.
    Task<IReadOnlyList<ProjectRecord>> ListFirstProjectsAsync(int limit, CancellationToken ct = default);

    Task<ProjectFull> GetProjectAsync(string id, CancellationToken ct = default);

    Task<ProjectRecord> CreateProjectAsync(CreateProjectRequest request, CancellationToken ct = default);

    Task<ProjectRecord> UpdateProjectAsync(string id, UpdateProjectRequest request, CancellationToken ct = default);

    Task DeleteProjectAsync(string id, CancellationToken ct = default);

    Task<byte[]> GetFileAsync(string id, string kind, CancellationToken ct = default);

    // The server is codec-free: dimensions and the extension hint ride the query, bytes the body.
    Task<FileWriteResult> PutFileAsync(string id, string kind, byte[] data, string ext, int w, int h, CancellationToken ct = default);

    // Filestore-only kinds (§9); idempotent and never bumps the project version.
    Task DeleteFileAsync(string id, string kind, CancellationToken ct = default);
}
