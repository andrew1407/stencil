using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Infrastructure.Server;
using Stencil.TelegramBot.Tests.Doubles;
using static Stencil.TelegramBot.Tests.Servers.ServerWireRig;

namespace Stencil.TelegramBot.Tests.Server;

/// <summary><see cref="HttpStencilServerClient.ListProjectsAsync"/> over a paging server: every page through <c>nextCursor</c>, one request when there is none, and a repeated cursor or an endless walk failing the listing instead of truncating it.</summary>
public sealed class HttpStencilServerClientPagingTests
{
    [Fact]
    public async Task Should_Walk_Every_Page_Following_Next_Cursor_On_List_Projects()
    {
        List<string> asked = new();
        CannedHttpMessageHandler handler = new((req, _) =>
        {
            asked.Add(req.RequestUri!.PathAndQuery);
            return CannedHttpMessageHandler.Json(asked.Count switch
            {
                1 => "{\"projects\":[{\"id\":\"p1\"},{\"id\":\"p2\"}],\"nextCursor\":\"1700/p_2 b\"}",
                2 => "{\"projects\":[{\"id\":\"p3\"},{\"id\":\"p4\"}],\"nextCursor\":\"1600/p_4\"}",
                _ => "{\"projects\":[{\"id\":\"p5\"}]}",
            });
        });
        HttpStencilServerClient client = new(new HttpClient(handler), "http://h:8090", "t") { ProjectListLimit = 2 };

        IReadOnlyList<ProjectRecord> projects = await client.ListProjectsAsync();

        Assert.Equal(["p1", "p2", "p3", "p4", "p5"], projects.Select(p => p.Id));
        Assert.Equal(
            ["/projects?limit=2", "/projects?limit=2&after=1700%2Fp_2%20b", "/projects?limit=2&after=1600%2Fp_4"],
            asked);
    }

    [Theory]
    [InlineData("{\"projects\":[{\"id\":\"p1\"}]}")]
    [InlineData("{\"projects\":[{\"id\":\"p1\"}],\"nextCursor\":\"\"}")]
    [InlineData("{\"projects\":[{\"id\":\"p1\"}],\"nextCursor\":7}")]
    public async Task Should_Make_One_Request_When_A_Page_Names_No_Cursor(string page)
    {
        int calls = 0;
        CannedHttpMessageHandler handler = new((_, _) => { calls++; return CannedHttpMessageHandler.Json(page); });

        IReadOnlyList<ProjectRecord> projects = await Client(handler, token: "t").ListProjectsAsync();

        Assert.Equal(1, calls);
        Assert.Equal("p1", Assert.Single(projects).Id);
    }

    [Fact]
    public async Task Should_Fail_A_Listing_Whose_Server_Repeats_A_Cursor()
    {
        int calls = 0;
        CannedHttpMessageHandler handler = new((_, _) =>
        {
            calls++;
            return CannedHttpMessageHandler.Json(calls == 2
                ? "{\"projects\":[{\"id\":\"p2\"}],\"nextCursor\":\"c1\"}"
                : "{\"projects\":[{\"id\":\"p1\"}],\"nextCursor\":\"c1\"}");
        });

        ServerException ex = await Assert.ThrowsAsync<ServerException>(
            () => Client(handler, token: "t").ListProjectsAsync());

        Assert.Equal("badResponse", ex.Code);
        Assert.Equal(2, calls);
    }

    [Fact]
    public async Task Should_Fail_A_Listing_That_Pages_Past_The_Page_Cap()
    {
        int calls = 0;
        CannedHttpMessageHandler handler = new((_, _) =>
            CannedHttpMessageHandler.Json($"{{\"projects\":[],\"nextCursor\":\"c{++calls}\"}}"));

        ServerException ex = await Assert.ThrowsAsync<ServerException>(
            () => Client(handler, token: "t").ListProjectsAsync());

        Assert.Equal("badResponse", ex.Code);
        Assert.Equal(HttpStencilServerClient.MAX_LIST_PAGES, calls);
    }

    [Fact]
    public async Task Should_Fail_A_Listing_Whose_Pages_Together_Pass_The_Reply_Cap()
    {
        int calls = 0;
        CannedHttpMessageHandler handler = new((_, _) =>
            CannedHttpMessageHandler.Json($"{{\"projects\":[{{\"id\":\"p{++calls}\"}}],\"nextCursor\":\"c{calls}\"}}"));
        HttpStencilServerClient client = new(new HttpClient(handler), "http://h:8090", "t") { MaxResponseBytes = 200 };

        ServerException ex = await Assert.ThrowsAsync<ServerException>(() => client.ListProjectsAsync());

        Assert.Equal("tooLarge", ex.Code);
        Assert.True(calls < 10);
    }

    [Fact]
    public async Task Should_Ask_For_One_Page_Only_On_List_First_Projects()
    {
        List<string> asked = new();
        CannedHttpMessageHandler handler = new((req, _) =>
        {
            asked.Add(req.RequestUri!.PathAndQuery);
            return CannedHttpMessageHandler.Json("{\"projects\":[{\"id\":\"p1\"}],\"nextCursor\":\"c1\"}");
        });

        IReadOnlyList<ProjectRecord> projects = await Client(handler, token: "t").ListFirstProjectsAsync(21);

        Assert.Equal("p1", Assert.Single(projects).Id);
        Assert.Equal(["/projects?limit=21"], asked);
    }
}
