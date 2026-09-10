using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Infrastructure.Cli;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Infrastructure.Workspace;
using Stencil.TelegramBot.Tests.Doubles;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// Colour validation, the bot's half of the port of <c>is_color</c> in <c>mcp/src/args.rs</c>:
/// the canonical <c>browser/js/config/colorNames.json</c> is embedded here too, and an
/// unparseable colour is refused before it reaches argv or the layout. Without this the CLI
/// silently SKIPS the colour — a <c>/blank</c> comes out white, a pen stroke stays default.
/// </summary>
public sealed class ColorSpecTests : IDisposable
{
    private const long UserId = 31;

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
    public void TheEmbeddedTableIsTheCanonical148Names() =>
        Assert.Equal(148, ColorSpec.KnownNames.Count);

    [Theory]
    [InlineData("transparent")]
    [InlineData("rebeccapurple")]
    [InlineData(" RebeccaPurple ")]
    [InlineData("#abc")]
    [InlineData("#abcd")]
    [InlineData("#ff5623")]
    [InlineData("#ff562380")]
    public void AParsableColourIsAccepted(string spec) => Assert.True(ColorSpec.IsValid(spec));

    [Theory]
    [InlineData(null)]
    [InlineData("")]
    [InlineData("   ")]
    [InlineData("notacolour")]
    [InlineData("#ab")]
    [InlineData("#abcde")]
    [InlineData("#gggggg")]
    [InlineData("rgb(1,2,3)")]
    public void AnUnparsableColourIsRejected(string? spec) => Assert.False(ColorSpec.IsValid(spec));

    // ── the negative tests: the bad colour never reaches the CLI ──

    [Fact]
    public void BlankRefusesAnUnknownColourInsteadOfBuildingArgv()
    {
        EditRequest request = new() { Blank = new BlankSpec(null, null, "puce", "A4"), Output = "/tmp/out.png" };

        StencilCliException ex = Assert.Throws<StencilCliException>(() => CliArgvBuilder.BuildArgv(request));

        Assert.Contains("puce", ex.Message);
    }

    [Fact]
    public void BlankStillAcceptsAKnownColour()
    {
        EditRequest request = new() { Blank = new BlankSpec(null, null, "pink", "A4"), Output = "/tmp/out.png" };

        Assert.Contains("pink", CliArgvBuilder.BuildArgv(request));
    }

    [Fact]
    public async Task PenColourRefusesAnUnknownColour()
    {
        InvalidOperationException ex = await Assert.ThrowsAsync<InvalidOperationException>(() =>
            _editing.ConfigurePenAsync(UserId, color: "puce", thickness: null, pointSize: null, style: null, fill: null));

        Assert.Contains("puce", ex.Message);
        Assert.Equal(LayoutLine.DefaultColor, (await _store.GetAsync(UserId, CancellationToken.None)).Edits.Pen.Color);
    }

    [Fact]
    public async Task PenFillRefusesAnUnknownColour() =>
        await Assert.ThrowsAsync<InvalidOperationException>(() =>
            _editing.ConfigurePenAsync(UserId, color: null, thickness: null, pointSize: null, style: null, fill: "puce"));

    [Fact]
    public async Task PenFillStillClearsWithNone()
    {
        await _editing.ConfigurePenAsync(UserId, color: "#00ff00", thickness: null, pointSize: null, style: null, fill: "none");

        Assert.Equal("transparent", (await _store.GetAsync(UserId, CancellationToken.None)).Edits.Pen.FillColor);
    }
}
