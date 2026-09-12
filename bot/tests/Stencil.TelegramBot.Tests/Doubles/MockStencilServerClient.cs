using System.Text.Json;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Tests.Doubles;

/// <summary>
/// An in-memory <see cref="IStencilServerClient"/>: keeps a dictionary of projects + layouts,
/// mints a token on a tokenless connect, version-guards updates (stale ⇒ a
/// <see cref="ServerException"/> conflict) and records the file uploads it received. No HTTP.
/// </summary>
public sealed class MockStencilServerClient : IStencilServerClient
{
    private readonly Dictionary<string, ProjectRecord> _projects = new();
    private readonly Dictionary<string, JsonElement> _layouts = new();
    private int _nextId = 1;

    public MockStencilServerClient(string baseUrl)
    {
        BaseUrl = baseUrl;
    }

    /// <inheritdoc />
    public string BaseUrl { get; }

    /// <summary>The token returned when a caller connects without one.</summary>
    public string MintedToken { get; set; } = "minted-token";

    /// <summary>When true, <see cref="ListProjectsAsync"/> throws (an unreachable server).</summary>
    public bool ThrowOnList { get; set; }

    /// <summary>Awaited inside <see cref="ListProjectsAsync"/>, so a test can hold several
    /// servers open at once and observe whether the caller fans out or queues.</summary>
    public Func<Task>? BeforeList { get; set; }

    /// <summary>The token the last <see cref="ConnectAsync"/> resolved to.</summary>
    public string? LastConnectToken { get; private set; }

    /// <summary>The credential kind <see cref="ConnectAsync"/> reports for a supplied token
    /// (a tokenless connect always reports <see cref="CredentialKind.None"/>).</summary>
    public CredentialKind HandshakeKind { get; set; } = CredentialKind.Session;

    /// <summary>Every <see cref="PutFileAsync"/> call, in order.</summary>
    public List<(string Id, string Kind, byte[] Data, string Ext, int W, int H)> Puts { get; } = new();

    /// <summary>Every <see cref="DeleteFileAsync"/> call, in order.</summary>
    public List<(string Id, string Kind)> FileDeletes { get; } = new();

    /// <summary>Bytes handed back by <see cref="GetFileAsync"/> for original/result kinds.</summary>
    public byte[] FileBytes { get; set; } = new byte[] { 1, 2, 3 };

    /// <summary>Per-kind stored file bytes (seeded directly or via <see cref="PutFileAsync"/>).</summary>
    public Dictionary<(string Id, string Kind), byte[]> Files { get; } = new();

    /// <summary>When set, <see cref="PutFileAsync"/> for this kind throws (an unreachable file route).</summary>
    public string? ThrowOnPutKind { get; set; }

    /// <summary>Seed a project (and optional layout) the way the real server would store it.</summary>
    public ProjectRecord Seed(ProjectRecord record, JsonElement? layout = null)
    {
        _projects[record.Id] = record;
        if (layout is JsonElement element)
        {
            _layouts[record.Id] = element.Clone();
        }
        return record;
    }

    /// <summary>Simulate another writer bumping the server-side version of a project.</summary>
    public void BumpVersion(string id)
    {
        ProjectRecord current = _projects[id];
        _projects[id] = current with { Version = current.Version + 1 };
    }

    /// <inheritdoc />
    public Task<ServerHandshake> ConnectAsync(string? token, CancellationToken ct = default)
    {
        bool anonymous = string.IsNullOrEmpty(token);
        LastConnectToken = anonymous ? MintedToken : token;
        return Task.FromResult(new ServerHandshake(
            LastConnectToken!, anonymous ? CredentialKind.None : HandshakeKind));
    }

    /// <inheritdoc />
    public Task<IReadOnlyList<ProjectRecord>> ListProjectsAsync(CancellationToken ct = default)
    {
        if (ThrowOnList)
        {
            throw new ServerException("unreachable", "server is down", 503);
        }
        IReadOnlyList<ProjectRecord> list = _projects.Values.ToList();
        return BeforeList is null ? Task.FromResult(list) : Gated(list);

        async Task<IReadOnlyList<ProjectRecord>> Gated(IReadOnlyList<ProjectRecord> records)
        {
            await BeforeList();
            return records;
        }
    }

    /// <inheritdoc />
    public Task<ProjectFull> GetProjectAsync(string id, CancellationToken ct = default)
    {
        ProjectRecord record = _projects[id];
        JsonElement? layout = _layouts.TryGetValue(id, out JsonElement stored) ? stored : null;
        ProjectFull full = new()
        {
            Project = record,
            Layout = layout,
            OriginalContent = null,
        };
        return Task.FromResult(full);
    }

    /// <inheritdoc />
    public Task<ProjectRecord> CreateProjectAsync(CreateProjectRequest request, CancellationToken ct = default)
    {
        string id = "p_" + _nextId++;
        ProjectRecord record = new()
        {
            Id = id,
            Name = request.Name ?? "Untitled",
            Description = request.Description,
            Color = request.Color,
            HasImage = request.HasImage,
            ImageW = request.ImageW,
            ImageH = request.ImageH,
            Version = 1,
        };
        _projects[id] = record;
        if (request.Layout is JsonElement layout)
        {
            _layouts[id] = layout.Clone();
        }
        return Task.FromResult(record);
    }

    /// <inheritdoc />
    public Task<ProjectRecord> UpdateProjectAsync(string id, UpdateProjectRequest request, CancellationToken ct = default)
    {
        if (!_projects.TryGetValue(id, out ProjectRecord? existing))
        {
            throw new ServerException("notFound", "no such project", 404);
        }
        if (request.Version != existing.Version)
        {
            throw new ServerException("conflict", "version conflict", 409);
        }
        ProjectRecord updated = existing with
        {
            Version = existing.Version + 1,
            Name = request.Name ?? existing.Name,
            Color = request.Color ?? existing.Color,
            Description = request.Description ?? existing.Description,
            BlankColor = request.BlankColor ?? existing.BlankColor,
            ExpiresAt = request.ExpiresAt ?? existing.ExpiresAt,
        };
        _projects[id] = updated;
        if (request.Layout is JsonElement layout)
        {
            _layouts[id] = layout.Clone();
        }
        return Task.FromResult(updated);
    }

    /// <inheritdoc />
    public Task DeleteProjectAsync(string id, CancellationToken ct = default)
    {
        _projects.Remove(id);
        _layouts.Remove(id);
        return Task.CompletedTask;
    }

    /// <inheritdoc />
    public Task<byte[]> GetFileAsync(string id, string kind, CancellationToken ct = default)
    {
        if (Files.TryGetValue((id, kind), out byte[]? stored))
        {
            return Task.FromResult(stored);
        }
        if (kind is ProjectFileKind.ORIGINAL or ProjectFileKind.RESULT)
        {
            return Task.FromResult(FileBytes);
        }
        // Filestore-only kinds (chat/video/variantN) 404 when nothing was stored, like the server.
        throw new ServerException("notFound", "no such file", 404);
    }

    /// <inheritdoc />
    public Task<FileWriteResult> PutFileAsync(string id, string kind, byte[] data, string ext, int w, int h, CancellationToken ct = default)
    {
        if (kind == ThrowOnPutKind)
        {
            throw new ServerException("unreachable", "file store is down", 503);
        }
        Puts.Add((id, kind, data, ext, w, h));
        Files[(id, kind)] = data;
        // The real server's SetFile bumps version/updated_at (store.go) but the response carries
        // no version — model that so clients that don't re-read the version afterwards are caught.
        // Filestore-only kinds (chat/video/variantN) never bump it (httpapi/files.go, contract §9).
        if (kind is ProjectFileKind.ORIGINAL or ProjectFileKind.RESULT
            && _projects.TryGetValue(id, out ProjectRecord? existing))
        {
            _projects[id] = existing with { Version = existing.Version + 1 };
        }
        return Task.FromResult(new FileWriteResult($"/store/{id}/{kind}.{ext}", w, h));
    }

    /// <inheritdoc />
    public Task DeleteFileAsync(string id, string kind, CancellationToken ct = default)
    {
        FileDeletes.Add((id, kind));
        Files.Remove((id, kind));
        return Task.CompletedTask; // idempotent 204, like the server (contract §9)
    }
}
