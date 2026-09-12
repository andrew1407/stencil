using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Infrastructure.Workspace;
using Stencil.TelegramBot.Tests.Doubles;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// §10 <c>connect</c>/<c>disconnect</c>: a plan may only re-run a connection the USER saved, and
/// never introduces a host or carries a token.
/// </summary>
public sealed class PromptConnectionTests : PromptServiceTestBase
{
    public PromptConnectionTests(PromptServiceFixture fixture) : base(fixture) { }

    [Fact]
    public async Task Should_Resolve_A_Saved_Server_By_Exact_Url_On_Connect_And_Let_Its_Stored_Token_Ride()
    {
        await SeedImage();
        await SeedConnections(Saved("http://alpha:8090", "tok-alpha"), Saved("https://beta:9090", "tok-beta"));
        RecordingServerService projects = new();
        PromptService service = WithProjects(projects);
        Reply("""{"reply":"connected","actions":[{"op":"connect","server":"http://alpha:8090"}]}""");

        PromptOutcome outcome = await service.PromptAsync(UserId, "connect to alpha", null, CancellationToken.None);

        Assert.Empty(outcome.Warnings);
        Assert.Equal([("http://alpha:8090", "tok-alpha", true)], projects.Connects);
        // Managing connections changed no pixels — nothing for the caller to render.
        Assert.False(outcome.Mutated);
    }

    [Fact]
    public async Task Should_Resolve_A_Unique_Host_Match_On_Connect_Like_The_Editors_Do()
    {
        await SeedImage();
        await SeedConnections(Saved("http://alpha:8090", "ta"), Saved("https://beta:9090", "tb"));
        RecordingServerService projects = new();
        PromptService service = WithProjects(projects);
        Reply("""{"reply":"ok","actions":[{"op":"connect","server":"beta"},{"op":"connect","server":"alpha:8090"}]}""");

        PromptOutcome outcome = await service.PromptAsync(UserId, "connect to beta, then alpha", null, CancellationToken.None);

        Assert.Empty(outcome.Warnings);
        Assert.Equal([("https://beta:9090", "tb", true), ("http://alpha:8090", "ta", true)], projects.Connects);
    }

    [Fact]
    public async Task Should_Warn_Instead_Of_Attempting_On_Connect_To_A_Server_The_User_Never_Saved()
    {
        await SeedImage();
        await SeedConnections(Saved("http://alpha:8090", "ta"));
        RecordingServerService projects = new();
        PromptService service = WithProjects(projects);
        Reply("""{"reply":"connecting","actions":[{"op":"connect","server":"http://evil.example:9"}]}""");

        PromptOutcome outcome = await service.PromptAsync(UserId, "connect", null, CancellationToken.None);

        // No REST call left the bot — an unsaved host is never contacted.
        Assert.Empty(projects.Connects);
        Assert.Contains(outcome.Warnings, w => w.Contains("connect it first with /connect"));
        Assert.Equal("connecting", outcome.Reply); // a warning, not a failed plan
    }

    [Fact]
    public async Task Should_Warn_That_The_Full_Url_Is_Needed_On_Connect_With_An_Ambiguous_Host()
    {
        await SeedImage();
        await SeedConnections(Saved("http://srv:8090", "t1"), Saved("https://srv:9090", "t2"));
        RecordingServerService projects = new();
        PromptService service = WithProjects(projects);
        Reply("""{"reply":"ok","actions":[{"op":"connect","server":"srv"}]}""");

        PromptOutcome outcome = await service.PromptAsync(UserId, "connect to srv", null, CancellationToken.None);

        Assert.Empty(projects.Connects);
        Assert.Contains(outcome.Warnings, w => w.Contains("matches several") && w.Contains("full URL"));
    }

    [Fact]
    public async Task Should_Resolve_Against_The_Connections_On_Disconnect_And_Forget_That_Server()
    {
        await SeedImage();
        await SeedConnections(Saved("http://alpha:8090"), Saved("https://beta:9090"));
        RecordingServerService projects = new();
        PromptService service = WithProjects(projects);
        Reply("""{"reply":"dropped","actions":[{"op":"disconnect","server":"beta"}]}""");

        PromptOutcome outcome = await service.PromptAsync(UserId, "disconnect beta", null, CancellationToken.None);

        Assert.Empty(outcome.Warnings);
        Assert.Equal(["https://beta:9090"], projects.Disconnects);
        Assert.False(outcome.Mutated);
    }

    [Fact]
    public async Task Should_Warn_When_Disconnecting_A_Server_That_Isnt_Connected()
    {
        await SeedImage();
        await SeedConnections(Saved("http://alpha:8090"));
        RecordingServerService projects = new();
        PromptService service = WithProjects(projects);
        Reply("""{"reply":"dropped","actions":[{"op":"disconnect","server":"gamma"}]}""");

        PromptOutcome outcome = await service.PromptAsync(UserId, "disconnect gamma", null, CancellationToken.None);

        Assert.Empty(projects.Disconnects);
        Assert.Contains(outcome.Warnings, w => w.Contains("isn't a connected server"));
        Assert.Equal("dropped", outcome.Reply);
    }

    [Fact]
    public async Task Should_Run_Connection_Ops_Without_A_Working_Image()
    {
        // "Connect to my server" must work before any photo is sent — the image pre-flight
        // only guards ops that touch pixels.
        await SeedConnections(Saved("http://alpha:8090", "ta"));
        RecordingServerService projects = new();
        PromptService service = WithProjects(projects);
        Reply("""{"reply":"connected","actions":[{"op":"connect","server":"alpha"}]}""");

        PromptOutcome outcome = await service.PromptAsync(UserId, "connect to alpha", null, CancellationToken.None);

        Assert.Equal("connected", outcome.Reply);
        Assert.Empty(outcome.Warnings);
        Assert.Equal([("http://alpha:8090", "ta", true)], projects.Connects);
        Assert.False(outcome.Mutated);
    }

    [Fact]
    public async Task Should_Warn_And_Still_Reply_When_A_Connect_Is_Refused()
    {
        await SeedImage();
        await SeedConnections(Saved("http://alpha:8090", "ta"));
        RecordingServerService projects = new() { ConnectFailWith = "token expired" };
        PromptService service = WithProjects(projects);
        Reply("""{"reply":"connected","actions":[{"op":"connect","server":"alpha"}]}""");

        PromptOutcome outcome = await service.PromptAsync(UserId, "connect to alpha", null, CancellationToken.None);

        Assert.Contains(outcome.Warnings, w => w.Contains("token expired"));
        Assert.Equal("connected", outcome.Reply);
    }
}
