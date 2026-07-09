using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Tests.Fakes;

/// <summary>
/// An <see cref="IServerService"/> that throws on every call — for handler tests whose command
/// (e.g. <c>/sourcesite</c>) never touches the collaboration server. If the handler under test
/// unexpectedly reaches the server surface, the test fails loudly instead of silently no-op'ing.
/// </summary>
public sealed class ThrowingServerService : IServerService
{
    private static T Fail<T>() => throw new NotSupportedException("the server service must not be called in this test");

    public Task<ServerConnectionInfo> ConnectAsync(long userId, string url, string? token, bool verifyTls, CancellationToken ct = default) => Fail<Task<ServerConnectionInfo>>();
    public Task<bool> DisconnectAsync(long userId, string? url, CancellationToken ct = default) => Fail<Task<bool>>();
    public Task<IReadOnlyList<ServerConnectionInfo>> ConnectionsAsync(long userId, CancellationToken ct = default) => Fail<Task<IReadOnlyList<ServerConnectionInfo>>>();
    public Task<IReadOnlyList<ServerProjectInfo>> ListProjectsAsync(long userId, string? url, CancellationToken ct = default) => Fail<Task<IReadOnlyList<ServerProjectInfo>>>();
    public Task<UserSession> FetchAsync(long userId, string nameOrId, string? url, CancellationToken ct = default) => Fail<Task<UserSession>>();
    public Task<ProjectRecord> CreateProjectAsync(long userId, string? name, string? url, CancellationToken ct = default) => Fail<Task<ProjectRecord>>();
    public Task<ProjectRecord> SaveActiveProjectAsync(long userId, CancellationToken ct = default) => Fail<Task<ProjectRecord>>();
    public Task<string> SetProjectColorAsync(long userId, string color, CancellationToken ct = default) => Fail<Task<string>>();
    public Task<string> GetProjectBlankColorAsync(long userId, CancellationToken ct = default) => Fail<Task<string>>();
    public Task<string> SetProjectBlankColorAsync(long userId, string color, CancellationToken ct = default) => Fail<Task<string>>();
    public Task<long> SetProjectExpiryAsync(long userId, long expiresAtMs, CancellationToken ct = default) => Fail<Task<long>>();
    public Task<string> DeleteActiveProjectAsync(long userId, CancellationToken ct = default) => Fail<Task<string>>();
    public Task<long?> ActiveServerVersionAsync(long userId, CancellationToken ct = default) => Fail<Task<long?>>();
    public Task<UserSession?> PullActiveAsync(long userId, CancellationToken ct = default) => Fail<Task<UserSession?>>();
}
