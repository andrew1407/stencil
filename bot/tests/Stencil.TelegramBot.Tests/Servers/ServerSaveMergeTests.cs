using System.Text.Json;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Serialization;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Tests.Doubles;

namespace Stencil.TelegramBot.Tests.Servers;

/// <summary>A save that meets a peer's newer version: the 409 re-reads the project, merges the peer's
/// lines with the session's through the CLI's <c>--merge-lines</c>, and saves both at the peer's version.</summary>
public sealed class ServerSaveMergeTests : ServerServiceTestBase
{
    private const string _projectId = "p_seed";

    private static LayoutLine line(double x, string color = "#FF0000") =>
        new() { Points = [new LayoutPoint(x, 1), new LayoutPoint(x, 9)], Color = color };

    private static JsonElement layoutOf(params LayoutLine[] lines) =>
        StencilJson.ToElement(new { imageFilter = "sepia", lines });

    private MockStencilServerClient server => _factory.ClientFor(ServerA);

    // The bot holds `local` at v4; a peer has saved `peer` as v5 since.
    private async Task fetchThenPeerSavesAsync(LayoutLine local, LayoutLine peer)
    {
        ProjectRecord seeded = new() { Id = _projectId, Name = "Shared", ImageW = 100, ImageH = 80, Version = 4 };
        server.Seed(seeded, layoutOf());
        await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);
        UserSession session = await _service.FetchAsync(UserId, "Shared", url: null);
        EditState edits = session.Edits with { Layout = new StencilLayout { Lines = [local] }, Filter = "bw" };
        await _store.SaveAsync(session with { Edits = edits });
        server.Seed(seeded with { Version = 5 }, layoutOf(peer));
    }

    private async Task<IReadOnlyList<LayoutLine>> serverLinesAsync()
    {
        ProjectFull full = await server.GetProjectAsync(_projectId);
        return StencilJson.FromElement<List<LayoutLine>>(full.Layout!.Value.GetProperty("lines"))!;
    }

    private static string keyOf(IEnumerable<LayoutLine> lines) => StencilJson.Serialize(lines);

    [Fact]
    public async Task Should_Save_Both_Users_Lines_After_One_Merge_Pass()
    {
        LayoutLine mine = line(10), theirs = line(50, "#0000FF");
        await fetchThenPeerSavesAsync(mine, theirs);

        ProjectRecord saved = await _service.SaveActiveProjectAsync(UserId);

        (IReadOnlyList<LayoutLine> peer, IReadOnlyList<LayoutLine> local, IReadOnlyList<LayoutLine> seen) = Assert.Single(_cli.Merges);
        Assert.Equal(keyOf([theirs]), keyOf(peer));
        Assert.Equal(keyOf([mine]), keyOf(local));
        Assert.Empty(seen);
        Assert.Equal(keyOf([theirs, mine]), keyOf(await serverLinesAsync()));
        // The merged lines and the version that names them are adopted together.
        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal(keyOf([theirs, mine]), keyOf(session.Edits.Layout!.Lines));
        Assert.Equal(7, saved.Version); // v6 the merged layout, v7 the result upload
        Assert.Equal(7, session.ActiveProjectVersion);
        // The bot keeps its own filter: a line merge never adopts the peer's.
        Assert.Equal("bw", session.Edits.Filter);
        Assert.Equal("bw", (await server.GetProjectAsync(_projectId)).Layout!.Value.GetProperty("imageFilter").GetString());
    }

    [Fact]
    public async Task Should_Re_Render_The_Result_From_The_Merged_Lines()
    {
        await fetchThenPeerSavesAsync(line(10), line(50, "#0000FF"));
        int before = _cli.EditCalls;

        await _service.SaveActiveProjectAsync(UserId);

        Assert.Equal(before + 2, _cli.EditCalls);
        Assert.Equal(2, StencilJson.FromElement<StencilLayout>(
            JsonDocument.Parse(_cli.LayoutJson(_cli.LastRequest!)!).RootElement)!.Lines.Count);
    }

    [Fact]
    public async Task Should_Pass_The_First_Passes_Peer_Lines_As_Seen_On_The_Second()
    {
        LayoutLine mine = line(10), first = line(50, "#0000FF"), second = line(70, "#00FF00");
        await fetchThenPeerSavesAsync(mine, first);
        // The peer saves once more between our first merge and its retry, deleting `first`.
        int updates = 0;
        server.BeforeUpdate = id =>
        {
            if (++updates == 2)
            {
                ProjectRecord current = server.GetProjectAsync(id).Result.Project;
                server.Seed(current with { Version = current.Version + 1 }, layoutOf(second));
            }
        };

        await _service.SaveActiveProjectAsync(UserId);

        Assert.Equal(2, _cli.Merges.Count);
        Assert.Equal(keyOf([first]), keyOf(_cli.Merges[1].Seen));
        Assert.Equal(keyOf([first, mine]), keyOf(_cli.Merges[1].Local));
        // The peer's delete of `first` is not resurrected by the line we merged from it.
        Assert.Equal(keyOf([second, mine]), keyOf(await serverLinesAsync()));
    }

    [Fact]
    public async Task Should_Save_Without_A_Merge_When_No_Peer_Wrote()
    {
        await SeedWorkingImageAsync();
        await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);
        await _service.CreateProjectAsync(UserId, "Doc", url: null);

        await _service.SaveActiveProjectAsync(UserId);

        Assert.Empty(_cli.Merges);
    }
}
