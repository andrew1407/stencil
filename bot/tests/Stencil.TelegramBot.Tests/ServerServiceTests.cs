using System.Text.Json;
using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Tests.Doubles;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// <see cref="ServerService"/>'s connect path (credential kinds, invite fragments),
/// cross-server listing and fetch with layout-filter seeding.
/// </summary>
public sealed class ServerServiceTests : ServerServiceTestBase
{
    [Fact]
    public async Task ConnectStoresConnectionWithMintedToken()
    {
        _factory.ClientFor(ServerA).MintedToken = "minted-xyz";

        ServerConnectionInfo info = await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);

        Assert.Equal("http://a:8090", info.Url);
        Assert.Equal("minted-xyz", info.Token);
        UserSession session = await _store.GetAsync(UserId);
        ServerConnectionInfo stored = Assert.Single(session.Connections);
        Assert.Equal("minted-xyz", stored.Token);
    }

    [Fact]
    public async Task ConnectPersistsTheCredentialAndReusesItForLaterClients()
    {
        ServerConnectionInfo info = await _service.ConnectAsync(UserId, ServerA, token: "adm-secret", verifyTls: true);

        // The user-supplied value rides beside the live session token…
        Assert.Equal("adm-secret", info.Credential);
        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal("adm-secret", Assert.Single(session.Connections).Credential);

        // …and every later client is built with it, so a stale session can re-mint.
        await _service.ListProjectsAsync(UserId, url: null);
        Assert.Equal("adm-secret", _factory.Created[^1].Credential);
    }

    [Fact]
    public async Task ConnectRecordsTheCredentialKindAndReusesItForLaterClients()
    {
        _factory.ClientFor(ServerA).HandshakeKind = CredentialKind.Admin;

        ServerConnectionInfo info = await _service.ConnectAsync(UserId, ServerA, token: "adm-secret", verifyTls: true);

        Assert.Equal(CredentialKind.Admin, info.CredentialKind);
        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal(CredentialKind.Admin, Assert.Single(session.Connections).CredentialKind);

        // Every later client is built with it, so a known admin token skips the doomed probe…
        await _service.ListProjectsAsync(UserId, url: null);
        Assert.Equal(CredentialKind.Admin, _factory.Created[^1].Kind);
        // …including a re-connect to the same origin.
        await _service.ConnectAsync(UserId, ServerA, token: "adm-secret", verifyTls: true);
        Assert.Equal(CredentialKind.Admin, _factory.Created[^1].Kind);
    }

    [Fact]
    public async Task ConnectRecordsASessionCredentialAndATokenlessConnectRecordsNone()
    {
        // A supplied token that validated directly is a plain session token…
        ServerConnectionInfo session = await _service.ConnectAsync(UserId, ServerA, token: "sess-tok", verifyTls: true);
        Assert.Equal(CredentialKind.Session, session.CredentialKind);

        // …while a tokenless connect mints anonymously: there is no credential to classify.
        ServerConnectionInfo anonymous = await _service.ConnectAsync(UserId, ServerB, token: null, verifyTls: true);
        Assert.Equal(CredentialKind.None, anonymous.CredentialKind);

        UserSession stored = await _store.GetAsync(UserId);
        Assert.Equal(CredentialKind.Session, stored.FindConnection(ServerA)!.CredentialKind);
        Assert.Equal(CredentialKind.None, stored.FindConnection(ServerB)!.CredentialKind);
    }

    [Fact]
    public async Task ConnectParsesInviteLinkFragmentToken()
    {
        ServerConnectionInfo info = await _service.ConnectAsync(UserId, ServerA + "#token=inv-tok", token: null, verifyTls: true);

        Assert.Equal("http://a:8090", info.Url);
        Assert.Equal("inv-tok", info.Token);
        Assert.Equal("inv-tok", info.Credential);
    }

    [Fact]
    public async Task ConnectExplicitTokenWinsOverInviteFragment()
    {
        ServerConnectionInfo info = await _service.ConnectAsync(UserId, ServerA + "#token=frag-tok", token: "explicit", verifyTls: true);

        Assert.Equal("http://a:8090", info.Url);
        Assert.Equal("explicit", info.Token);
        Assert.Equal("explicit", info.Credential);
    }

    [Fact]
    public async Task ConnectWithoutFragmentIsUnchanged()
    {
        _factory.ClientFor(ServerA).MintedToken = "minted-1";

        ServerConnectionInfo info = await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);

        Assert.Equal("http://a:8090", info.Url);
        Assert.Equal("minted-1", info.Token);
        Assert.Equal("", info.Credential);
    }

    [Fact]
    public async Task ConnectPersistsInviteFragmentTokenAsCredential()
    {
        await _service.ConnectAsync(UserId, ServerA + "#token=inv-adm", token: null, verifyTls: true);

        // The fragment token rides the stored connection like a typed one…
        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal("inv-adm", Assert.Single(session.Connections).Credential);
        // …and every later client is built with it, so a stale session can re-mint.
        await _service.ListProjectsAsync(UserId, url: null);
        Assert.Equal("inv-adm", _factory.Created[^1].Credential);
    }

    [Fact]
    public async Task ListProjectsAggregatesAndSkipsAThrowingServer()
    {
        _factory.ClientFor(ServerA).Seed(new ProjectRecord { Id = "p_a", Name = "Alpha" });
        _factory.ClientFor(ServerB).ThrowOnList = true;
        await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);
        await _service.ConnectAsync(UserId, ServerB, token: null, verifyTls: true);

        IReadOnlyList<ServerProjectInfo> projects = await _service.ListProjectsAsync(UserId, url: null);

        ServerProjectInfo only = Assert.Single(projects);
        Assert.Equal("p_a", only.Record.Id);
        Assert.Equal("http://a:8090", only.ServerUrl);
    }

    [Fact]
    public async Task FetchSetsActiveProjectAndSeedsFilterFromLayout()
    {
        _factory.ClientFor(ServerA).Seed(
            new ProjectRecord { Id = "p_seed", Name = "Shared", ImageW = 320, ImageH = 240, Version = 4 },
            LayoutWithFilter("sepia"));
        await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);

        UserSession session = await _service.FetchAsync(UserId, "Shared", url: null);

        Assert.Equal("p_seed", session.ActiveProjectId);
        Assert.Equal("Shared", session.ActiveProjectName);
        Assert.Equal("http://a:8090", session.ActiveServerUrl);
        Assert.Equal(4, session.ActiveProjectVersion);
        Assert.Equal("sepia", session.Edits.Filter);
        Assert.True(session.HasImage);
        Assert.True(File.Exists(session.OriginalImagePath));
    }

    [Fact]
    public async Task FetchThrowsWhenNoMatch()
    {
        await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);
        await Assert.ThrowsAsync<InvalidOperationException>(
            () => _service.FetchAsync(UserId, "Nope", url: null));
    }

}
