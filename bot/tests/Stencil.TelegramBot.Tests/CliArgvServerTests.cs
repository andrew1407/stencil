using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Infrastructure.Cli;

namespace Stencil.TelegramBot.Tests;

/// <summary>The collaboration-server flags of <see cref="CliArgvBuilder"/> (<c>--server</c>/<c>--remote-update</c>/<c>--remote</c>/<c>--remote-name</c>, <c>cli/CONTRACT.md</c> §1) and the combinations it refuses.</summary>
public sealed class CliArgvServerTests
{
    [Fact]
    public void Should_Build_Server_Fetch_And_Remote_Update()
    {
        EditRequest req = new()
        {
            Server = "http://h:8090",
            Input = "Shared",
            Filter = "sepia",
            RemoteUpdate = true,
            Output = "out.png",
        };
        Assert.Equal(
            new[]
            {
                "--server", "http://h:8090",
                "-i", "Shared",
                "--filter", "sepia",
                "--remote-update",
                "--confine-output", "out.png",
            },
            CliArgvBuilder.BuildArgv(req));
    }

    [Fact]
    public void Should_Build_Remote_Create_With_Name()
    {
        EditRequest req = new()
        {
            Input = "photo.png",
            Rotate = 1,
            Remote = "http://h:8090",
            RemoteName = "Shared",
            Output = "out.png",
        };
        Assert.Equal(
            new[]
            {
                "-i", "photo.png",
                "-r", "1",
                "--remote", "http://h:8090",
                "--remote-name", "Shared",
                "--confine-output", "out.png",
            },
            CliArgvBuilder.BuildArgv(req));
    }

    [Fact]
    public void Should_Fetch_From_One_Server_And_Publish_To_Another()
    {
        EditRequest req = new()
        {
            Server = "http://a:8090",
            Input = "Plans",
            Remote = "http://b:8090",
            RemoteName = "Plans copy",
            Output = "out.png",
        };
        IReadOnlyList<string> argv = CliArgvBuilder.BuildArgv(req);
        Assert.Equal(new[] { "--server", "http://a:8090" }, argv.Take(2));
        int r = argv.ToList().IndexOf("--remote");
        Assert.Equal("http://b:8090", argv[r + 1]);
    }

    [Fact]
    public void Should_Reject_Server_Without_Input()
    {
        EditRequest req = new()
        {
            Server = "http://h:8090",
            Output = "out.png",
        };
        StencilCliException ex = Assert.Throws<StencilCliException>(() => CliArgvBuilder.BuildArgv(req));
        Assert.Contains("server", ex.Message);
    }

    [Fact]
    public void Should_Reject_Server_With_Blank()
    {
        // blank carries a source, so `input` is absent — `server` still can't take a blank.
        EditRequest req = new()
        {
            Server = "http://h:8090",
            Blank = new BlankSpec(),
            Output = "out.png",
        };
        StencilCliException ex = Assert.Throws<StencilCliException>(() => CliArgvBuilder.BuildArgv(req));
        Assert.Contains("blank", ex.Message);
    }

    [Fact]
    public void Should_Reject_Remote_Update_Without_Server()
    {
        EditRequest req = new()
        {
            Input = "a.png",
            RemoteUpdate = true,
            Output = "out.png",
        };
        StencilCliException ex = Assert.Throws<StencilCliException>(() => CliArgvBuilder.BuildArgv(req));
        Assert.Contains("remote_update", ex.Message);
    }

    [Fact]
    public void Should_Reject_Remote_Name_Without_Remote()
    {
        EditRequest req = new()
        {
            Input = "a.png",
            RemoteName = "X",
            Output = "out.png",
        };
        StencilCliException ex = Assert.Throws<StencilCliException>(() => CliArgvBuilder.BuildArgv(req));
        Assert.Contains("remote_name", ex.Message);
    }
}
