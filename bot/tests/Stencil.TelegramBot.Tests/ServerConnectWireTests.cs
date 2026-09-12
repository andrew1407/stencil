using System.Net;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Server;
using Stencil.TelegramBot.Tests.Doubles;
using static Stencil.TelegramBot.Tests.ServerWireRig;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The token handshake of <see cref="HttpStencilServerClient"/>: how a token is classified,
/// when an admin credential mints a session token, and which probe rejections propagate.
/// </summary>
public sealed class ServerConnectWireTests
{
    [Fact]
    public async Task ConnectWithoutTokenPostsAuthTokenAndReturnsIt()
    {
        CannedHttpMessageHandler handler = new((_, _) =>
            CannedHttpMessageHandler.Json("{\"token\":\"minted-abc\",\"expiresAt\":999}"));
        HttpStencilServerClient client = Client(handler, token: null);

        ServerHandshake handshake = await client.ConnectAsync(null);

        Assert.Equal("minted-abc", handshake.Token);
        // Nothing was supplied, so there is no credential to classify.
        Assert.Equal(CredentialKind.NONE, handshake.CredentialKind);
        Assert.Equal(HttpMethod.Post, handler.LastRequest!.Method);
        Assert.Equal("/auth/token", handler.LastRequest.RequestUri!.AbsolutePath);
    }

    [Fact]
    public async Task ConnectWithASessionTokenThatListsIsClassifiedAsASession()
    {
        List<string> paths = new();
        CannedHttpMessageHandler handler = new((req, _) =>
        {
            paths.Add(req.RequestUri!.AbsolutePath);
            return CannedHttpMessageHandler.Json("{\"projects\":[]}");
        });
        HttpStencilServerClient client = Client(handler, token: null);

        ServerHandshake handshake = await client.ConnectAsync("sess-tok");

        Assert.Equal("sess-tok", handshake.Token);
        Assert.Equal(CredentialKind.SESSION, handshake.CredentialKind);
        Assert.Equal(["/projects"], paths.ToArray()); // validated directly, nothing minted
    }

    [Fact]
    public async Task ConnectWithAdminTokenMintsSessionTokenAndRevalidates()
    {
        List<(string Path, string? Bearer)> calls = new();
        CannedHttpMessageHandler handler = new((req, _) =>
        {
            calls.Add((req.RequestUri!.AbsolutePath, req.Headers.Authorization?.Parameter));
            if (req.RequestUri.AbsolutePath == "/auth/token")
            {
                return CannedHttpMessageHandler.Json("{\"token\":\"sess-1\"}");
            }
            // /projects: the admin token can't list, the minted session token can.
            return req.Headers.Authorization?.Parameter == "sess-1"
                ? CannedHttpMessageHandler.Json("{\"projects\":[]}")
                : CannedHttpMessageHandler.Json(
                    "{\"code\":\"unauthorized\",\"message\":\"admin cannot list\"}", HttpStatusCode.Unauthorized);
        });
        HttpStencilServerClient client = Client(handler, token: null);

        ServerHandshake handshake = await client.ConnectAsync("adm-secret");

        Assert.Equal("sess-1", handshake.Token);
        // The mint-then-validate round PROVED the credential is the server's admin token.
        Assert.Equal(CredentialKind.ADMIN, handshake.CredentialKind);
        Assert.Equal(["/projects", "/auth/token", "/projects"], calls.Select(c => c.Path).ToArray());
        Assert.Equal("adm-secret", calls[1].Bearer); // the mint carried the admin token as bearer
    }

    [Fact]
    public async Task ConnectWithAKnownAdminCredentialSkipsTheDoomedProbe()
    {
        List<(string Path, string? Bearer)> calls = new();
        CannedHttpMessageHandler handler = new((req, _) =>
        {
            calls.Add((req.RequestUri!.AbsolutePath, req.Headers.Authorization?.Parameter));
            return req.RequestUri.AbsolutePath == "/auth/token"
                ? CannedHttpMessageHandler.Json("{\"token\":\"sess-2\"}")
                : req.Headers.Authorization?.Parameter == "sess-2"
                    ? CannedHttpMessageHandler.Json("{\"projects\":[]}")
                    : CannedHttpMessageHandler.Json(
                        "{\"code\":\"unauthorized\",\"message\":\"admin cannot list\"}", HttpStatusCode.Unauthorized);
        });
        HttpStencilServerClient client = new(new HttpClient(handler), "http://h:8090", null,
            credential: "adm-secret", credentialKind: CredentialKind.ADMIN);

        ServerHandshake handshake = await client.ConnectAsync("adm-secret");

        Assert.Equal("sess-2", handshake.Token);
        Assert.Equal(CredentialKind.ADMIN, handshake.CredentialKind);
        // Mint first, then prove the session works — no 401 probe spent up front.
        Assert.Equal(["/auth/token", "/projects"], calls.Select(c => c.Path).ToArray());
        Assert.Equal("adm-secret", calls[0].Bearer);
    }

    [Fact]
    public async Task ConnectWithWrongTokenSurfacesTheProbeRejection()
    {
        int requests = 0;
        CannedHttpMessageHandler handler = new((req, _) =>
        {
            requests++;
            return req.RequestUri!.AbsolutePath == "/auth/token"
                ? CannedHttpMessageHandler.Json(
                    "{\"code\":\"unauthorized\",\"message\":\"admin token required\"}", HttpStatusCode.Unauthorized)
                : CannedHttpMessageHandler.Json(
                    "{\"code\":\"unauthorized\",\"message\":\"invalid token\"}", HttpStatusCode.Unauthorized);
        });
        HttpStencilServerClient client = Client(handler, token: null);

        ServerException ex = await Assert.ThrowsAsync<ServerException>(() => client.ConnectAsync("wrong"));

        Assert.Equal(401, ex.Status);
        Assert.Contains("invalid token", ex.Message); // the probe's error, not the mint's
        Assert.Equal(2, requests); // probe + one mint attempt, no loop
    }

    [Fact]
    public async Task ConnectNonAuthProbeErrorPropagatesWithoutMinting()
    {
        int requests = 0;
        CannedHttpMessageHandler handler = new((_, _) =>
        {
            requests++;
            return CannedHttpMessageHandler.Json(
                "{\"code\":\"internal\",\"message\":\"boom\"}", HttpStatusCode.InternalServerError);
        });
        HttpStencilServerClient client = Client(handler, token: null);

        ServerException ex = await Assert.ThrowsAsync<ServerException>(() => client.ConnectAsync("tok"));

        Assert.Equal(500, ex.Status);
        Assert.Equal(1, requests); // only auth failures trigger the mint fallback
    }
}
