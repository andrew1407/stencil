using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Tests.Doubles;

namespace Stencil.TelegramBot.Tests.Servers;

/// <summary><see cref="ServerService"/>'s connect path (credential kinds, invite fragments), cross-server listing and fetch with layout-filter seeding.</summary>
public sealed class ServerServiceTests : ServerServiceTestBase
{
    [Fact]
    public async Task Should_Store_Connection_With_Minted_Token_On_Connect()
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
    public async Task Should_Persist_The_Credential_And_Reuse_It_For_Later_Clients_On_Connect()
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
    public async Task Should_Record_The_Credential_Kind_And_Reuse_It_For_Later_Clients_On_Connect()
    {
        _factory.ClientFor(ServerA).HandshakeKind = CredentialKind.ADMIN;

        ServerConnectionInfo info = await _service.ConnectAsync(UserId, ServerA, token: "adm-secret", verifyTls: true);

        Assert.Equal(CredentialKind.ADMIN, info.CredentialKind);
        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal(CredentialKind.ADMIN, Assert.Single(session.Connections).CredentialKind);

        // Every later client is built with it, so a known admin token skips the doomed probe…
        await _service.ListProjectsAsync(UserId, url: null);
        Assert.Equal(CredentialKind.ADMIN, _factory.Created[^1].Kind);
        // …including a re-connect to the same origin.
        await _service.ConnectAsync(UserId, ServerA, token: "adm-secret", verifyTls: true);
        Assert.Equal(CredentialKind.ADMIN, _factory.Created[^1].Kind);
    }

    [Fact]
    public async Task Should_Record_A_Session_Credential_On_Connect_And_None_On_A_Tokenless_Connect()
    {
        // A supplied token that validated directly is a plain session token…
        ServerConnectionInfo session = await _service.ConnectAsync(UserId, ServerA, token: "sess-tok", verifyTls: true);
        Assert.Equal(CredentialKind.SESSION, session.CredentialKind);

        // …while a tokenless connect mints anonymously: there is no credential to classify.
        ServerConnectionInfo anonymous = await _service.ConnectAsync(UserId, ServerB, token: null, verifyTls: true);
        Assert.Equal(CredentialKind.NONE, anonymous.CredentialKind);

        UserSession stored = await _store.GetAsync(UserId);
        Assert.Equal(CredentialKind.SESSION, stored.FindConnection(ServerA)!.CredentialKind);
        Assert.Equal(CredentialKind.NONE, stored.FindConnection(ServerB)!.CredentialKind);
    }

    [Fact]
    public async Task Should_Parse_The_Invite_Link_Fragment_Token_On_Connect()
    {
        ServerConnectionInfo info = await _service.ConnectAsync(UserId, ServerA + "#token=inv-tok", token: null, verifyTls: true);

        Assert.Equal("http://a:8090", info.Url);
        Assert.Equal("inv-tok", info.Token);
        Assert.Equal("inv-tok", info.Credential);
    }

    [Fact]
    public async Task Should_Prefer_An_Explicit_Token_Over_The_Invite_Fragment_On_Connect()
    {
        ServerConnectionInfo info = await _service.ConnectAsync(UserId, ServerA + "#token=frag-tok", token: "explicit", verifyTls: true);

        Assert.Equal("http://a:8090", info.Url);
        Assert.Equal("explicit", info.Token);
        Assert.Equal("explicit", info.Credential);
    }

    [Fact]
    public async Task Should_Leave_Connect_Unchanged_Without_A_Fragment()
    {
        _factory.ClientFor(ServerA).MintedToken = "minted-1";

        ServerConnectionInfo info = await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);

        Assert.Equal("http://a:8090", info.Url);
        Assert.Equal("minted-1", info.Token);
        Assert.Equal("", info.Credential);
    }

    [Fact]
    public async Task Should_Persist_The_Invite_Fragment_Token_As_Credential_On_Connect()
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
    public async Task Should_Set_Active_Project_And_Seed_Filter_From_Layout_On_Fetch()
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
    public async Task Should_Throw_On_Fetch_When_Nothing_Matches()
    {
        await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);
        await Assert.ThrowsAsync<InvalidOperationException>(
            () => _service.FetchAsync(UserId, "Nope", url: null));
    }

    // Otherwise every later call re-mints, and the sync poll trips the server's token limit.
    [Fact]
    public async Task Should_Persist_A_Token_The_Client_Re_Minted()
    {
        MockStencilServerClient server = _factory.ClientFor(ServerA);
        server.Seed(new ProjectRecord { Id = "p_seed", Name = "Shared", Version = 4 }, LayoutWithFilter("none"));
        await _service.ConnectAsync(UserId, ServerA, token: "adm-secret", verifyTls: true);
        await _service.FetchAsync(UserId, "Shared", url: null);
        server.Session = new ServerHandshake("re-minted", CredentialKind.ADMIN);

        await _service.ActiveServerVersionAsync(UserId);

        ServerConnectionInfo stored = Assert.Single((await _store.GetAsync(UserId)).Connections);
        Assert.Equal("re-minted", stored.Token);
        Assert.Equal(CredentialKind.ADMIN, stored.CredentialKind);
        Assert.Equal("adm-secret", stored.Credential);
    }

    [Fact]
    public async Task Should_Open_A_Project_By_Id_Without_Listing_And_Keep_An_Unchanged_Original()
    {
        MockStencilServerClient server = _factory.ClientFor(ServerA);
        ProjectRecord seeded = server.Seed(
            new ProjectRecord { Id = "p_seed", Name = "Shared", Version = 4, OriginalHash = "h1" }, LayoutWithFilter("none"));
        await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);
        server.ThrowOnList = true; // an id never walks the listing

        UserSession first = await _service.FetchAsync(UserId, "p_seed", url: null);
        server.Seed(seeded with { Version = 5 }, LayoutWithFilter("bw")); // a peer's layout-only save
        UserSession pulled = (await _service.PullActiveAsync(UserId))!;

        Assert.Equal(["original"], server.FileGets);
        Assert.Equal(first.OriginalImagePath, pulled.OriginalImagePath);
        Assert.Equal(5, pulled.ActiveProjectVersion);
        server.Seed(seeded with { Version = 6, OriginalHash = "h2" });
        await _service.PullActiveAsync(UserId);
        Assert.Equal(["original", "original"], server.FileGets);
    }
}
