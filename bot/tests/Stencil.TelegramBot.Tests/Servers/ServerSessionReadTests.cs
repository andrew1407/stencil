using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Tests.Doubles;

namespace Stencil.TelegramBot.Tests.Servers;

/// <summary>The session reads around a server call: an unchanged token costs none, and the version poll
/// swallows a server failure but never a cancellation.</summary>
public sealed class ServerSessionReadTests : ServerServiceTestBase
{
    // A client that still holds the token it was built with costs no store read; a fetch hands back what it saved.
    [Fact]
    public async Task Should_Not_Re_Read_The_Session_When_The_Token_Is_Unchanged()
    {
        CountingSessionStore store = new(_store);
        ServerService service = new(_factory, store, _editing, _cli);
        MockStencilServerClient server = _factory.ClientFor(ServerA);
        server.Seed(new ProjectRecord { Id = "p_seed", Name = "Shared", Version = 4 }, LayoutWithFilter("none"));
        await service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);
        server.Session = new ServerHandshake(server.MintedToken, CredentialKind.NONE);

        store.Gets = 0;
        Assert.Equal("p_seed", (await service.FetchAsync(UserId, "p_seed", url: null)).ActiveProjectId);
        Assert.Equal(1, store.Gets);
        store.Gets = 0;
        Assert.Equal(4, await service.ActiveServerVersionAsync(UserId));
        Assert.Equal(1, store.Gets);
    }

    // The sync poll treats a server failure as "unreachable", never the caller's cancellation.
    [Fact]
    public async Task Should_Let_A_Cancellation_Through_The_Version_Poll()
    {
        MockStencilServerClient server = _factory.ClientFor(ServerA);
        server.Seed(new ProjectRecord { Id = "p_seed", Name = "Shared", Version = 4 });
        await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);
        await _service.FetchAsync(UserId, "p_seed", url: null);

        server.ThrowOnGet = new ServerException("unreachable", "down", 503);
        Assert.Null(await _service.ActiveServerVersionAsync(UserId));
        server.ThrowOnGet = new OperationCanceledException();
        await Assert.ThrowsAsync<OperationCanceledException>(() => _service.ActiveServerVersionAsync(UserId));
    }

    private sealed class CountingSessionStore(ISessionStore inner) : ISessionStore
    {
        public int Gets { get; set; }

        public Task<UserSession> GetAsync(long userId, CancellationToken ct = default)
        {
            Gets++;
            return inner.GetAsync(userId, ct);
        }

        public Task SaveAsync(UserSession session, CancellationToken ct = default) => inner.SaveAsync(session, ct);

        public Task ResetAsync(long userId, CancellationToken ct = default) => inner.ResetAsync(userId, ct);
    }
}
