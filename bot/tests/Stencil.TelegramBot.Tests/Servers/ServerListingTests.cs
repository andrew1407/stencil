using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Projects;

namespace Stencil.TelegramBot.Tests.Servers;

/// <summary><see cref="ServerService.ListProjectsAsync"/>'s multi-server fan-out: every connection asked at once, unreachable ones skipped, the result in connection order.</summary>
public sealed class ServerListingTests : ServerServiceTestBase
{
    [Fact]
    public async Task Should_Aggregate_And_Skip_A_Throwing_Server_On_List_Projects()
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
    public async Task Should_Ask_Every_Server_At_Once_On_List_Projects()
    {
        _factory.ClientFor(ServerA).Seed(new ProjectRecord { Id = "p_a", Name = "Alpha" });
        _factory.ClientFor(ServerB).Seed(new ProjectRecord { Id = "p_b", Name = "Beta" });
        await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);
        await _service.ConnectAsync(UserId, ServerB, token: null, verifyTls: true);
        // Neither server answers until BOTH have been entered, so a queued fan-out cannot finish.
        TaskCompletionSource bothEntered = new(TaskCreationOptions.RunContinuationsAsynchronously);
        int entered = 0;
        Func<Task> gate = () =>
        {
            if (Interlocked.Increment(ref entered) == 2)
            {
                bothEntered.TrySetResult();
            }
            return bothEntered.Task;
        };
        _factory.ClientFor(ServerA).BeforeList = gate;
        _factory.ClientFor(ServerB).BeforeList = gate;

        Task<IReadOnlyList<ServerProjectInfo>> listing = _service.ListProjectsAsync(UserId, url: null);
        Task finished = await Task.WhenAny(listing, Task.Delay(TimeSpan.FromSeconds(5)));

        Assert.Same(listing, finished);
        IReadOnlyList<ServerProjectInfo> projects = await listing;
        // Still in connection order, not reply order.
        Assert.Equal(["p_a", "p_b"], projects.Select(p => p.Record.Id));
        Assert.Equal([ServerA, ServerB], projects.Select(p => p.ServerUrl));
    }
}
