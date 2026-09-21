using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Infrastructure.Cli;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Infrastructure.Workspace;
using Stencil.TelegramBot.Tests.Doubles;

namespace Stencil.TelegramBot.Tests.Editing;

/// <summary>Colour validation, the bot's half of the port of <c>is_color</c> in <c>mcp/src/args.rs</c> over the embedded canonical <c>colorNames.json</c>: the CLI silently SKIPS an unparseable colour, so it is refused before argv.</summary>
public sealed class ColorSpecTests : IDisposable
{
    private const long _userId = 31;

    private readonly string _dataDir;
    private readonly MockStencilCli _cli = new();
    private readonly InMemorySessionStore _store = new();
    private readonly EditingService _editing;

    public ColorSpecTests()
    {
        _dataDir = Path.Combine(Path.GetTempPath(), "stencil-bot-color-" + Guid.NewGuid().ToString("N"));
        BotOptions options = new() { DataDir = _dataDir, AllowedUsers = AnyUser.Instance };
        _editing = new EditingService(_cli, new UserWorkspace(options), _store);
    }

    public void Dispose()
    {
        try { Directory.Delete(_dataDir, recursive: true); } catch { /* best effort */ }
    }

    [Fact]
    public void Should_Embed_The_Canonical_148_Names_In_The_Table() =>
        Assert.Equal(148, ColorSpec.KnownNames.Count);

    [Theory]
    [InlineData("transparent")]
    [InlineData("rebeccapurple")]
    [InlineData(" RebeccaPurple ")]
    [InlineData("#abc")]
    [InlineData("#abcd")]
    [InlineData("#ff5623")]
    [InlineData("#ff562380")]
    public void Should_Accept_A_Parsable_Colour(string spec) => Assert.True(ColorSpec.IsValid(spec));

    [Theory]
    [InlineData(null)]
    [InlineData("")]
    [InlineData("   ")]
    [InlineData("notacolour")]
    [InlineData("#ab")]
    [InlineData("#abcde")]
    [InlineData("#gggggg")]
    [InlineData("rgb(1,2,3)")]
    public void Should_Reject_An_Unparsable_Colour(string? spec) => Assert.False(ColorSpec.IsValid(spec));

    // ── the negative tests: the bad colour never reaches the CLI ──

    [Fact]
    public void Should_Refuse_An_Unknown_Colour_Instead_Of_Building_Argv_On_Blank()
    {
        EditRequest request = new() { Blank = new BlankSpec(null, null, "puce", "A4"), Output = "/tmp/out.png" };

        StencilCliException ex = Assert.Throws<StencilCliException>(() => CliArgvBuilder.BuildArgv(request));

        Assert.Contains("puce", ex.Message);
    }

    [Fact]
    public void Should_Still_Accept_A_Known_Colour_On_Blank()
    {
        EditRequest request = new() { Blank = new BlankSpec(null, null, "pink", "A4"), Output = "/tmp/out.png" };

        Assert.Contains("pink", CliArgvBuilder.BuildArgv(request));
    }

    [Fact]
    public async Task Should_Refuse_An_Unknown_Colour_On_Pen_Colour()
    {
        InvalidOperationException ex = await Assert.ThrowsAsync<InvalidOperationException>(() =>
            _editing.ConfigurePenAsync(_userId, color: "puce", thickness: null, pointSize: null, style: null, fill: null));

        Assert.Contains("puce", ex.Message);
        Assert.Equal(LayoutLine.DEFAULT_COLOR, (await _store.GetAsync(_userId, CancellationToken.None)).Edits.Pen.Color);
    }

    [Fact]
    public async Task Should_Refuse_An_Unknown_Colour_On_Pen_Fill() =>
        await Assert.ThrowsAsync<InvalidOperationException>(() =>
            _editing.ConfigurePenAsync(_userId, color: null, thickness: null, pointSize: null, style: null, fill: "puce"));

    [Fact]
    public async Task Should_Still_Clear_With_None_On_Pen_Fill()
    {
        await _editing.ConfigurePenAsync(_userId, color: "#00ff00", thickness: null, pointSize: null, style: null, fill: "none");

        Assert.Equal("transparent", (await _store.GetAsync(_userId, CancellationToken.None)).Edits.Pen.FillColor);
    }
}
