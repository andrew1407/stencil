using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Domain.Abstractions;

// A port of pystencil's ServerConnection. REST only: no live /ws feed. Every non-2xx surfaces
// as ServerException.
public interface IStencilServerClient
{
    // scheme://host[:port].
    string BaseUrl { get; }

    // No token: mint one via POST /auth/token. With one: validate it by listing projects — and
    // when that probe is refused, the value may be the server's ADMIN token, so mint a session
    // token with it as bearer and adopt that instead.
    Task<ServerHandshake> ConnectAsync(string? token, CancellationToken ct = default);

    // GET /projects, newest-updated first.
    Task<IReadOnlyList<ProjectRecord>> ListProjectsAsync(CancellationToken ct = default);

    // GET /projects/{id}, with its layout and original payload.
    Task<ProjectFull> GetProjectAsync(string id, CancellationToken ct = default);

    // POST /projects.
    Task<ProjectRecord> CreateProjectAsync(CreateProjectRequest request, CancellationToken ct = default);

    // PUT /projects/{id}; 409 = conflict.
    Task<ProjectRecord> UpdateProjectAsync(string id, UpdateProjectRequest request, CancellationToken ct = default);

    // DELETE /projects/{id}.
    Task DeleteProjectAsync(string id, CancellationToken ct = default);

    // GET /projects/{id}/files/{kind}.
    Task<byte[]> GetFileAsync(string id, string kind, CancellationToken ct = default);

    // POST /projects/{id}/files/{kind}?ext&w&h. The server is codec-free, so dimensions and the
    // extension hint are passed in while the pixel bytes go in the octet-stream body.
    Task<FileWriteResult> PutFileAsync(string id, string kind, byte[] data, string ext, int w, int h, CancellationToken ct = default);

    // DELETE /projects/{id}/files/{kind}: filestore-only kinds (§9 — chat/video/variantN).
    // Idempotent (an absent file still answers 204) and never bumps the project version.
    Task DeleteFileAsync(string id, string kind, CancellationToken ct = default);
}
