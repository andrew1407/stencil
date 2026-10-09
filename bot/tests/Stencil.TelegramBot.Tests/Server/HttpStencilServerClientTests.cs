using System.Net;
using System.Text;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Infrastructure.Server;
using Stencil.TelegramBot.Tests.Doubles;
using static Stencil.TelegramBot.Tests.Servers.ServerWireRig;

namespace Stencil.TelegramBot.Tests.Server;

/// <summary>The REST calls of <see cref="HttpStencilServerClient"/>: the bearer header, listing, octet-stream file upload/delete and the structured <c>{code, message}</c> error mapping. Ports <c>pystencil</c>'s <c>ServerConnection</c> tests.</summary>
public sealed class HttpStencilServerClientTests
{
    [Fact]
    public async Task Should_Send_The_Bearer_Header_On_A_Listed_Call()
    {
        CannedHttpMessageHandler handler = new((_, _) =>
            CannedHttpMessageHandler.Json("{\"projects\":[]}"));
        HttpStencilServerClient client = Client(handler, token: "tok-123");

        await client.ListProjectsAsync();

        Assert.Equal("Bearer", handler.LastRequest!.Headers.Authorization!.Scheme);
        Assert.Equal("tok-123", handler.LastRequest.Headers.Authorization.Parameter);
        Assert.Equal("/projects", handler.LastRequest.RequestUri!.AbsolutePath);
    }

    [Theory]
    [InlineData(null, "?limit=200")] // the default page
    [InlineData(7, "?limit=7")]
    public async Task Should_Ask_For_A_Bounded_First_Page_On_List_Projects(int? limit, string query)
    {
        CannedHttpMessageHandler handler = new((_, _) => CannedHttpMessageHandler.Json("{\"projects\":[]}"));
        HttpStencilServerClient client = limit is int n
            ? new(new HttpClient(handler), "http://h:8090", "t") { ProjectListLimit = n }
            : Client(handler, token: "t");

        await client.ListProjectsAsync();

        Assert.Equal(query, handler.LastRequest!.RequestUri!.Query);
    }

    [Fact]
    public async Task Should_Refuse_A_Reply_Past_The_Response_Cap()
    {
        CannedHttpMessageHandler handler = new((req, _) => req.RequestUri!.AbsolutePath.EndsWith("/original")
            ? new HttpResponseMessage(HttpStatusCode.OK) { Content = new ByteArrayContent(new byte[64]) }
            : CannedHttpMessageHandler.Json("{\"projects\":[{\"id\":\"p1\",\"name\":\"a long enough name\"}]}"));
        HttpStencilServerClient client = new(new HttpClient(handler), "http://h:8090", "t") { MaxResponseBytes = 16 };

        ServerException listed = await Assert.ThrowsAsync<ServerException>(() => client.ListProjectsAsync());
        ServerException file = await Assert.ThrowsAsync<ServerException>(
            () => client.GetFileAsync("p1", ProjectFileKind.ORIGINAL));

        Assert.Equal("tooLarge", listed.Code);
        Assert.Equal("tooLarge", file.Code);
    }

    [Fact]
    public async Task Should_Fall_Back_To_The_Status_When_An_Error_Body_Is_Oversized()
    {
        string huge = "{\"code\":\"x\",\"message\":\"" + new string('m', 128 * 1024) + "\"}";
        CannedHttpMessageHandler handler = new((_, _) =>
            CannedHttpMessageHandler.Json(huge, HttpStatusCode.InternalServerError));
        HttpStencilServerClient client = Client(handler, token: "t");

        ServerException ex = await Assert.ThrowsAsync<ServerException>(() => client.ListProjectsAsync());

        Assert.Equal(500, ex.Status);
        Assert.Equal("HTTP 500", ex.Message);
    }

    [Fact]
    public async Task Should_Parse_The_Array_On_List_Projects()
    {
        CannedHttpMessageHandler handler = new((_, _) =>
            CannedHttpMessageHandler.Json(
                "{\"projects\":[{\"id\":\"p1\",\"name\":\"A\",\"imageW\":10,\"imageH\":20,\"expiresAt\":1700009999000,\"version\":3}," +
                "{\"id\":\"p2\",\"name\":\"B\"}]}"));
        HttpStencilServerClient client = Client(handler, token: "t");

        IReadOnlyList<ProjectRecord> projects = await client.ListProjectsAsync();

        Assert.Equal(2, projects.Count);
        Assert.Equal("p1", projects[0].Id);
        Assert.Equal(10, projects[0].ImageW);
        Assert.Equal(3, projects[0].Version);
        Assert.Equal(1700009999000, projects[0].ExpiresAt);
        Assert.Equal(0, projects[1].ExpiresAt); // absent → 0 (keep forever)
    }

    [Fact]
    public async Task Should_Send_Query_And_Octet_Stream_Body_On_Put_File()
    {
        byte[] payload = Encoding.UTF8.GetBytes("PIXELS");
        CannedHttpMessageHandler handler = new((_, _) =>
            CannedHttpMessageHandler.Json("{\"path\":\"/store/p1/original.png\",\"w\":800,\"h\":600}"));
        HttpStencilServerClient client = Client(handler, token: "t");

        FileWriteResult result = await client.PutFileAsync("p1", ProjectFileKind.ORIGINAL, payload, "png", 800, 600);

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
    public async Task Should_Send_Delete_To_The_File_Route_And_Accept_No_Content_On_Delete_File()
    {
        CannedHttpMessageHandler handler = new((_, _) =>
            CannedHttpMessageHandler.Empty(HttpStatusCode.NoContent));
        HttpStencilServerClient client = Client(handler, token: "t");

        await client.DeleteFileAsync("p1", ProjectFileKind.CHAT);

        Assert.Equal(HttpMethod.Delete, handler.LastRequest!.Method);
        Assert.Equal("/projects/p1/files/chat", handler.LastRequest.RequestUri!.AbsolutePath);
        Assert.Equal("Bearer", handler.LastRequest.Headers.Authorization!.Scheme);
    }

    [Fact]
    public async Task Should_Throw_Server_Exception_With_Code_And_Status_For_A_Non_Success_Body()
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
    public async Task Should_Yield_Is_Conflict_For_A_Conflict_Response()
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

    // HttpClient.Timeout cancels a token the caller never cancelled: a reply, not a silent shutdown.
    [Fact]
    public async Task Should_Report_A_Server_That_Never_Answers_As_A_Timeout()
    {
        CannedHttpMessageHandler handler = new((_, _) => CannedHttpMessageHandler.Json("{}")) { Hang = true };
        HttpStencilServerClient client = new(new HttpClient(handler) { Timeout = TimeSpan.FromMilliseconds(50) }, "http://h:8090", "t");

        ServerException ex = await Assert.ThrowsAsync<ServerException>(() => client.GetProjectAsync("p1"));

        Assert.Equal("timeout", ex.Code);
        Assert.Contains("http://h:8090 didn't answer", ex.Message);
    }

    [Fact]
    public async Task Should_Leave_A_Callers_Own_Cancellation_A_Cancellation()
    {
        CannedHttpMessageHandler handler = new((_, _) => CannedHttpMessageHandler.Json("{}")) { Hang = true };
        HttpStencilServerClient client = Client(handler, token: "t");
        using CancellationTokenSource shutdown = new(TimeSpan.FromMilliseconds(50));

        await Assert.ThrowsAnyAsync<OperationCanceledException>(() => client.GetProjectAsync("p1", shutdown.Token));
    }
}
