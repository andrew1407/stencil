using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Domain.Abstractions;

/// <summary>
/// A REST client for one Stencil collaboration server — a faithful port of
/// <c>pystencil</c>'s <c>ServerConnection</c> (and the browser net layer it ports). REST
/// only: no live <c>/ws</c> feed. Every non-2xx surfaces as
/// <see cref="Exceptions.ServerException"/>.
/// </summary>
public interface IStencilServerClient
{
    /// <summary>The normalised origin this client talks to (<c>scheme://host[:port]</c>).</summary>
    string BaseUrl { get; }

    /// <summary>
    /// Acquire or validate a token (handshake): with no token, mint one via
    /// <c>POST /auth/token</c>; with a token, validate it by listing projects — and when that
    /// probe is refused as unauthorised, the value may be the server's ADMIN token: mint a
    /// session token with it as bearer and adopt that instead. Returns the effective token
    /// plus the <see cref="CredentialKind"/> the handshake proved.
    /// </summary>
    Task<ServerHandshake> ConnectAsync(string? token, CancellationToken ct = default);

    /// <summary><c>GET /projects</c> → the project records, newest-updated first.</summary>
    Task<IReadOnlyList<ProjectRecord>> ListProjectsAsync(CancellationToken ct = default);

    /// <summary><c>GET /projects/{id}</c> → the project plus its layout and original payload.</summary>
    Task<ProjectFull> GetProjectAsync(string id, CancellationToken ct = default);

    /// <summary><c>POST /projects</c> → the created record.</summary>
    Task<ProjectRecord> CreateProjectAsync(CreateProjectRequest request, CancellationToken ct = default);

    /// <summary><c>PUT /projects/{id}</c> → the updated record (409 ⇒ conflict).</summary>
    Task<ProjectRecord> UpdateProjectAsync(string id, UpdateProjectRequest request, CancellationToken ct = default);

    /// <summary><c>DELETE /projects/{id}</c>.</summary>
    Task DeleteProjectAsync(string id, CancellationToken ct = default);

    /// <summary><c>GET /projects/{id}/files/{kind}</c> → raw image bytes.</summary>
    Task<byte[]> GetFileAsync(string id, string kind, CancellationToken ct = default);

    /// <summary>
    /// <c>POST /projects/{id}/files/{kind}?ext&amp;w&amp;h</c> → the stored path/dimensions.
    /// The server is codec-free, so the dimensions and extension hint are passed in while the
    /// pixel bytes go in the octet-stream body.
    /// </summary>
    Task<FileWriteResult> PutFileAsync(string id, string kind, byte[] data, string ext, int w, int h, CancellationToken ct = default);

    /// <summary>
    /// <c>DELETE /projects/{id}/files/{kind}</c> — remove a filestore-only kind's bytes
    /// (contract §9: valid for <c>chat</c>/<c>video</c>/<c>variantN</c> only). Idempotent (an
    /// absent file still answers 204) and never bumps the project version.
    /// </summary>
    Task DeleteFileAsync(string id, string kind, CancellationToken ct = default);
}
