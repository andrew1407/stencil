using System.Net;
using System.Text;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Infrastructure.Server;
using Stencil.TelegramBot.Tests.Fakes;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// Wire-contract behaviour for <see cref="HttpStencilServerClient"/> driven by a captured-request
/// fake handler (no network): the token handshake, bearer header, listing, octet-stream file
/// upload, and structured <c>{code, message}</c> error mapping. Ports <c>pystencil</c>'s
/// <c>ServerConnection</c> tests.
/// </summary>
public sealed class HttpStencilServerClientTests
{
    private static HttpStencilServerClient Client(CannedHttpMessageHandler handler, string? token = "") =>
        new(new HttpClient(handler), "http://h:8090", token);

    [Fact]
    public async Task ConnectWithoutTokenPostsAuthTokenAndReturnsIt()
    {
        CannedHttpMessageHandler handler = new((_, _) =>
            CannedHttpMessageHandler.Json("{\"token\":\"minted-abc\",\"expiresAt\":999}"));
        HttpStencilServerClient client = Client(handler, token: null);

        string token = await client.ConnectAsync(null);

        Assert.Equal("minted-abc", token);
        Assert.Equal(HttpMethod.Post, handler.LastRequest!.Method);
        Assert.Equal("/auth/token", handler.LastRequest.RequestUri!.AbsolutePath);
    }

    [Fact]
    public async Task BearerHeaderIsPresentOnAListedCall()
    {
        CannedHttpMessageHandler handler = new((_, _) =>
            CannedHttpMessageHandler.Json("{\"projects\":[]}"));
        HttpStencilServerClient client = Client(handler, token: "tok-123");

        await client.ListProjectsAsync();

        Assert.Equal("Bearer", handler.LastRequest!.Headers.Authorization!.Scheme);
        Assert.Equal("tok-123", handler.LastRequest.Headers.Authorization.Parameter);
        Assert.Equal("/projects", handler.LastRequest.RequestUri!.AbsolutePath);
    }

    [Fact]
    public async Task ListProjectsParsesArray()
    {
        CannedHttpMessageHandler handler = new((_, _) =>
            CannedHttpMessageHandler.Json(
                "{\"projects\":[{\"id\":\"p1\",\"name\":\"A\",\"imageW\":10,\"imageH\":20,\"version\":3}," +
                "{\"id\":\"p2\",\"name\":\"B\"}]}"));
        HttpStencilServerClient client = Client(handler, token: "t");

        IReadOnlyList<ProjectRecord> projects = await client.ListProjectsAsync();

        Assert.Equal(2, projects.Count);
        Assert.Equal("p1", projects[0].Id);
        Assert.Equal(10, projects[0].ImageW);
        Assert.Equal(3, projects[0].Version);
    }

    [Fact]
    public async Task PutFileSendsQueryAndOctetStreamBody()
    {
        byte[] payload = Encoding.UTF8.GetBytes("PIXELS");
        CannedHttpMessageHandler handler = new((_, _) =>
            CannedHttpMessageHandler.Json("{\"path\":\"/store/p1/original.png\",\"w\":800,\"h\":600}"));
        HttpStencilServerClient client = Client(handler, token: "t");

        FileWriteResult result = await client.PutFileAsync("p1", ProjectFileKind.Original, payload, "png", 800, 600);

        Assert.Equal(HttpMethod.Post, handler.LastRequest!.Method);
        Assert.Equal("/projects/p1/files/original", handler.LastRequest.RequestUri!.AbsolutePath);
        string query = handler.LastRequest.RequestUri.Query;
        Assert.Contains("ext=png", query);
        Assert.Contains("w=800", query);
        Assert.Contains("h=600", query);
        Assert.Equal("application/octet-stream", handler.LastContentType);
        Assert.Equal(payload, handler.LastBody);
        Assert.Equal("/store/p1/original.png", result.Path);
        Assert.Equal(800, result.W);
        Assert.Equal(600, result.H);
    }

    [Fact]
    public async Task NonSuccessBodyThrowsServerExceptionWithCodeAndStatus()
    {
        CannedHttpMessageHandler handler = new((_, _) =>
            CannedHttpMessageHandler.Json(
                "{\"code\":\"badRequest\",\"message\":\"nope\"}", HttpStatusCode.BadRequest));
        HttpStencilServerClient client = Client(handler, token: "t");

        ServerException ex = await Assert.ThrowsAsync<ServerException>(() => client.ListProjectsAsync());

        Assert.Equal("badRequest", ex.Code);
        Assert.Equal(400, ex.Status);
        Assert.Contains("nope", ex.Message);
    }

    [Fact]
    public async Task ConflictResponseYieldsIsConflict()
    {
        CannedHttpMessageHandler handler = new((_, _) =>
            CannedHttpMessageHandler.Json(
                "{\"code\":\"conflict\",\"message\":\"stale\"}", HttpStatusCode.Conflict));
        HttpStencilServerClient client = Client(handler, token: "t");
        UpdateProjectRequest request = new() { Version = 1 };

        ServerException ex = await Assert.ThrowsAsync<ServerException>(
            () => client.UpdateProjectAsync("p1", request));

        Assert.True(ex.IsConflict);
        Assert.Equal(409, ex.Status);
    }
}
