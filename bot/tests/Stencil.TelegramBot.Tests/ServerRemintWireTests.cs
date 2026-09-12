using System.Net;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Infrastructure.Server;
using Stencil.TelegramBot.Tests.Doubles;
using static Stencil.TelegramBot.Tests.ServerWireRig;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The mid-session re-mint: a stale session token is re-minted once from the stored credential
/// and the call retried — never looped, and never without a credential to mint from.
/// </summary>
public sealed class ServerRemintWireTests
{
    [Fact]
    public async Task Should_Remint_Once_With_The_Credential_And_Retry_On_A_Stale_Session_Token()
    {
        List<string> bearers = new();
        CannedHttpMessageHandler handler = new((req, _) =>
        {
            bearers.Add(req.Headers.Authorization?.Parameter ?? "");
            if (req.RequestUri!.AbsolutePath == "/auth/token")
            {
                return CannedHttpMessageHandler.Json("{\"token\":\"fresh\"}");
            }
            return req.Headers.Authorization?.Parameter == "fresh"
                ? CannedHttpMessageHandler.Json("{\"projects\":[{\"id\":\"p1\",\"name\":\"A\"}]}")
                : CannedHttpMessageHandler.Json(
                    "{\"code\":\"unauthorized\",\"message\":\"unknown token\"}", HttpStatusCode.Unauthorized);
        });
        HttpStencilServerClient client = new(new HttpClient(handler), "http://h:8090", "stale", credential: "cred");

        IReadOnlyList<ProjectRecord> projects = await client.ListProjectsAsync();

        Assert.Single(projects);
        Assert.Equal(["stale", "cred", "fresh"], bearers.ToArray());
    }

    [Fact]
    public async Task Should_Promote_The_Credential_To_Admin_On_A_Mid_Session_Remint()
    {
        CannedHttpMessageHandler handler = new((req, _) =>
        {
            if (req.RequestUri!.AbsolutePath == "/auth/token")
            {
                return CannedHttpMessageHandler.Json("{\"token\":\"fresh\"}");
            }
            return req.Headers.Authorization?.Parameter == "fresh"
                ? CannedHttpMessageHandler.Json("{\"projects\":[]}")
                : CannedHttpMessageHandler.Json(
                    "{\"code\":\"unauthorized\",\"message\":\"unknown token\"}", HttpStatusCode.Unauthorized);
        });
        HttpStencilServerClient client = new(new HttpClient(handler), "http://h:8090", "stale", credential: "cred");

        await client.ListProjectsAsync(); // the rescue round mints with the credential and works

        // …so the credential is now known to be an admin token, and the next handshake says so.
        ServerHandshake handshake = await client.ConnectAsync("cred");
        Assert.Equal("fresh", handshake.Token);
        Assert.Equal(CredentialKind.ADMIN, handshake.CredentialKind);
    }

    [Fact]
    public async Task Should_Not_Loop_And_Keep_The_Original_Error_On_Auth_Token_Failure()
    {
        int requests = 0;
        CannedHttpMessageHandler handler = new((_, _) =>
        {
            requests++;
            return CannedHttpMessageHandler.Json(
                "{\"code\":\"unauthorized\",\"message\":\"session over\"}", HttpStatusCode.Unauthorized);
        });
        HttpStencilServerClient client = new(new HttpClient(handler), "http://h:8090", "stale", credential: "cred");

        ServerException ex = await Assert.ThrowsAsync<ServerException>(() => client.ListProjectsAsync());

        Assert.Equal(401, ex.Status);
        Assert.Contains("session over", ex.Message);
        Assert.Equal(2, requests); // original + one mint attempt — never retried or recursed
    }

    [Fact]
    public async Task Should_Not_Remint_On_Auth_Failure_Without_A_Credential()
    {
        int requests = 0;
        CannedHttpMessageHandler handler = new((_, _) =>
        {
            requests++;
            return CannedHttpMessageHandler.Json(
                "{\"code\":\"unauthorized\",\"message\":\"nope\"}", HttpStatusCode.Unauthorized);
        });
        HttpStencilServerClient client = Client(handler, token: "sess");

        await Assert.ThrowsAsync<ServerException>(() => client.ListProjectsAsync());

        Assert.Equal(1, requests);
    }
}
