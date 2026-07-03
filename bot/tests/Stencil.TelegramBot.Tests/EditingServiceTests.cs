using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Infrastructure.Workspace;
using Stencil.TelegramBot.Tests.Fakes;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// <see cref="EditingService"/> over a <see cref="FakeStencilCli"/>, a real
/// <see cref="InMemorySessionStore"/> and a real <see cref="UserWorkspace"/> rooted at a temp
/// directory: the edit-state accumulation, rotate wrap, render-request mapping and JSON export.
/// </summary>
public sealed class EditingServiceTests : IDisposable
{
    private const long UserId = 1234;

    private readonly string _root;
    private readonly FakeStencilCli _cli;
    private readonly InMemorySessionStore _store;
    private readonly EditingService _service;

    public EditingServiceTests()
    {
        _root = Path.Combine(Path.GetTempPath(), "stencil-editing-" + Guid.NewGuid().ToString("N"));
        BotOptions options = new() { DataDir = _root };
        UserWorkspace workspace = new(options);
        _cli = new FakeStencilCli();
        _store = new InMemorySessionStore();
        _service = new EditingService(_cli, workspace, _store);
    }

    public void Dispose()
    {
        if (Directory.Exists(_root))
        {
            Directory.Delete(_root, recursive: true);
        }
    }

    [Fact]
    public async Task BlankSetsTheOriginal()
    {
        _cli.CannedSize = new ImageSize(595, 842);
        UserSession session = await _service.BlankAsync(UserId, new BlankSpec());
        Assert.True(session.HasImage);
        Assert.NotNull(session.OriginalImagePath);
        Assert.Equal(595, session.OriginalWidth);
        Assert.Equal(842, session.OriginalHeight);
        Assert.Equal("blank", session.ImageLabel);
        Assert.True(File.Exists(session.OriginalImagePath));
    }

    [Fact]
    public async Task CropRotateFilterAccumulateAndPersist()
    {
        await _service.BlankAsync(UserId, new BlankSpec());
        await _service.SetCropAsync(UserId, "x1=10% x2=90%", album: true);
        await _service.RotateAsync(UserId, 1);
        await _service.SetFilterAsync(UserId, "sepia");

        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal("x1=10% x2=90%", session.Edits.CropSpec);
        Assert.True(session.Edits.Album);
        Assert.Equal(1, session.Edits.Rotate);
        Assert.Equal("sepia", session.Edits.Filter);
    }

    [Fact]
    public async Task RotateWrapsModuloFour()
    {
        await _service.BlankAsync(UserId, new BlankSpec());
        await _service.RotateAsync(UserId, 3);
        UserSession after = await _service.RotateAsync(UserId, 3);
        Assert.Equal(2, after.Edits.Rotate);
    }

    [Fact]
    public async Task PageFormatAccumulatesOnTheEditState()
    {
        await _service.SetPageFormatAsync(UserId, "B5");
        UserSession named = await _store.GetAsync(UserId);
        Assert.Equal("B5", named.Edits.PageFormat);
        Assert.Null(named.Edits.CustomPageWidth);
        Assert.False(named.Edits.IsEmpty);

        await _service.SetPageFormatAsync(UserId, "custom", 10, 15.5);
        UserSession custom = await _store.GetAsync(UserId);
        Assert.Equal("custom", custom.Edits.PageFormat);
        Assert.Equal(10, custom.Edits.CustomPageWidth);
        Assert.Equal(15.5, custom.Edits.CustomPageHeight);

        await _service.SetPageFormatAsync(UserId, "A4"); // back to named — the custom dims drop
        UserSession back = await _store.GetAsync(UserId);
        Assert.Equal("A4", back.Edits.PageFormat);
        Assert.Null(back.Edits.CustomPageWidth);
    }

    [Fact]
    public async Task StoredFormatIsTheBlankDefaultPageAndSurvivesTheReset()
    {
        await _service.SetPageFormatAsync(UserId, "B5");
        UserSession session = await _service.BlankAsync(UserId, new BlankSpec());

        Assert.Equal("B5", _cli.LastRequest!.Blank!.Page); // rides --blank as the default page
        Assert.Equal("B5", session.Edits.PageFormat);      // …and stays set for the layout save
    }

    [Fact]
    public async Task ExplicitBlankPageWinsOverTheStoredFormat()
    {
        await _service.SetPageFormatAsync(UserId, "B5");
        UserSession session = await _service.BlankAsync(UserId, new BlankSpec(Page: "A5"));

        Assert.Equal("A5", _cli.LastRequest!.Blank!.Page);
        Assert.Equal("A5", session.Edits.PageFormat);
    }

    [Fact]
    public async Task CustomFormatRidesTheBlankFlagAsPixelDims()
    {
        await _service.SetPageFormatAsync(UserId, "custom", 10, 15);
        UserSession session = await _service.BlankAsync(UserId, new BlankSpec());

        // --blank takes named formats only, so custom cm dims convert to pixels the same
        // way the CLI console does (defaultBlankSizePx: cm / 2.54 * 96 dpi, rounded).
        Assert.Null(_cli.LastRequest!.Blank!.Page);
        Assert.Equal(378, _cli.LastRequest!.Blank!.Width);   // 10 cm @ 96 dpi
        Assert.Equal(567, _cli.LastRequest!.Blank!.Height);  // 15 cm @ 96 dpi
        Assert.Equal("custom", session.Edits.PageFormat);    // …and the layout still carries it
        Assert.Equal(10, session.Edits.CustomPageWidth);
        Assert.Equal(15, session.Edits.CustomPageHeight);
    }

    [Fact]
    public async Task CustomFormatWithoutDimsIsNotCarriedOntoTheDefaultBlank()
    {
        // A "custom" format missing its cm dims can't drive the raster, so the blank falls
        // back to the CLI's default page and must not be mislabeled custom in the layout.
        await _service.SetPageFormatAsync(UserId, "custom", null, null);
        UserSession session = await _service.BlankAsync(UserId, new BlankSpec());

        Assert.Null(_cli.LastRequest!.Blank!.Page);
        Assert.Null(_cli.LastRequest!.Blank!.Width);
        Assert.Null(_cli.LastRequest!.Blank!.Height);
        Assert.Null(session.Edits.PageFormat);
    }

    [Fact]
    public async Task ExplicitBlankPageWinsOverTheStoredCustomFormat()
    {
        await _service.SetPageFormatAsync(UserId, "custom", 10, 15);
        UserSession session = await _service.BlankAsync(UserId, new BlankSpec(Page: "A5"));

        Assert.Equal("A5", _cli.LastRequest!.Blank!.Page);
        Assert.Null(_cli.LastRequest!.Blank!.Width);
        Assert.Equal("A5", session.Edits.PageFormat);
        Assert.Null(session.Edits.CustomPageWidth);
    }

    [Fact]
    public async Task ExplicitBlankDimensionsKeepTheStoredFormat()
    {
        await _service.SetPageFormatAsync(UserId, "B5");
        UserSession session = await _service.BlankAsync(UserId, new BlankSpec(800, 600));

        Assert.Null(_cli.LastRequest!.Blank!.Page); // explicit w/h — no page token injected
        Assert.Equal(800, _cli.LastRequest!.Blank!.Width);
        // Explicit dims size the blank but preserve the /format pick across the reset,
        // matching the CLI console's doBlank (see console_test.zig's bot-parity test).
        Assert.Equal("B5", session.Edits.PageFormat);
    }

    [Fact]
    public async Task FilterNoneClearsTheFilter()
    {
        await _service.BlankAsync(UserId, new BlankSpec());
        await _service.SetFilterAsync(UserId, "bw");
        UserSession cleared = await _service.SetFilterAsync(UserId, "none");
        Assert.Null(cleared.Edits.Filter);
    }

    [Fact]
    public async Task RenderBuildsRequestCarryingEditsAndLayoutPath()
    {
        UserSession seeded = await _service.BlankAsync(UserId, new BlankSpec());
        await _service.SetCropAsync(UserId, "x1=5% x2=95%", album: false);
        await _service.RotateAsync(UserId, 1);
        await _service.SetFilterAsync(UserId, "bw");
        StencilLayout layout = new()
        {
            Lines = [new LayoutLine { Points = [new LayoutPoint(0, 0), new LayoutPoint(1, 1)] }],
        };
        await _service.ApplyLayoutAsync(UserId, layout);

        RenderResult result = await _service.RenderAsync(UserId);

        EditRequest request = _cli.LastRequest!;
        Assert.Equal(seeded.OriginalImagePath, request.Input);
        Assert.Equal("x1=5% x2=95%", request.CropSpec);
        Assert.Equal(1, request.Rotate);
        Assert.Equal("bw", request.Filter);
        Assert.NotNull(request.LayoutPath);
        Assert.True(File.Exists(request.LayoutPath));
        Assert.True(File.Exists(result.Path));
    }

    [Fact]
    public async Task RenderWithNoImageThrows()
    {
        await Assert.ThrowsAsync<InvalidOperationException>(() => _service.RenderAsync(UserId));
    }

    [Fact]
    public async Task DrawingAppendsLinesStyledWithThePen()
    {
        await _service.BlankAsync(UserId, new BlankSpec());
        await _service.ConfigurePenAsync(UserId, color: "#ff0000", thickness: 5, markerSize: 0, style: "dashed", fill: "#00ff00");

        await _service.AddLineAsync(UserId, [new LayoutPoint(0, 0), new LayoutPoint(10, 10)], closed: false);
        UserSession afterOpen = await _store.GetAsync(UserId);
        LayoutLine open = afterOpen.Edits.Layout!.Lines.Single();
        Assert.Equal("#ff0000", open.Color);
        Assert.Equal(5, open.Thickness);
        Assert.Equal("dashed", open.Style);
        Assert.False(open.Locked);
        Assert.Equal(LayoutLine.DefaultFillColor, open.FillColor); // open lines are never filled

        await _service.AddLineAsync(UserId, [new LayoutPoint(0, 0), new LayoutPoint(10, 0), new LayoutPoint(10, 10)], closed: true);
        UserSession afterClosed = await _store.GetAsync(UserId);
        Assert.Equal(2, afterClosed.Edits.LineCount);
        LayoutLine closed = afterClosed.Edits.Layout!.Lines[1];
        Assert.True(closed.Locked);
        Assert.Equal("#00ff00", closed.FillColor);
        Assert.Equal(new LayoutPoint(0, 0), closed.Points[^1]); // first point repeated to close
    }

    [Fact]
    public async Task UndoStepsBackThroughEdits()
    {
        await _service.BlankAsync(UserId, new BlankSpec());
        await _service.SetCropAsync(UserId, "x1=10%", album: false);
        await _service.SetFilterAsync(UserId, "bw");

        UserSession undo1 = await _service.UndoAsync(UserId);
        Assert.Null(undo1.Edits.Filter);            // the filter is undone…
        Assert.Equal("x1=10%", undo1.Edits.CropSpec); // …the crop remains

        UserSession undo2 = await _service.UndoAsync(UserId);
        Assert.Null(undo2.Edits.CropSpec);          // the crop is undone

        UserSession undo3 = await _service.UndoAsync(UserId);
        Assert.True(undo3.Edits.IsEmpty);           // nothing left to undo — no-op
    }

    [Fact]
    public async Task RedoReappliesUndoneEditsUntilANewEditClearsIt()
    {
        await _service.BlankAsync(UserId, new BlankSpec());
        await _service.SetFilterAsync(UserId, "bw");
        await _service.UndoAsync(UserId);                       // filter undone

        UserSession redone = await _service.RedoAsync(UserId);   // …and redone
        Assert.Equal("bw", redone.Edits.Filter);

        await _service.UndoAsync(UserId);                        // undo again
        await _service.SetFilterAsync(UserId, "sepia");          // a NEW edit clears the redo stack
        UserSession after = await _service.RedoAsync(UserId);    // nothing to redo now
        Assert.Equal("sepia", after.Edits.Filter);
    }

    [Fact]
    public async Task RemoveLastLineThenClearLines()
    {
        await _service.BlankAsync(UserId, new BlankSpec());
        await _service.AddLineAsync(UserId, [new LayoutPoint(0, 0), new LayoutPoint(1, 1)], closed: false);
        await _service.AddLineAsync(UserId, [new LayoutPoint(2, 2), new LayoutPoint(3, 3)], closed: false);

        UserSession afterRemove = await _service.RemoveLastLineAsync(UserId);
        Assert.Equal(1, afterRemove.Edits.LineCount);

        UserSession afterClear = await _service.ClearLinesAsync(UserId);
        Assert.Equal(0, afterClear.Edits.LineCount);
        Assert.Null(afterClear.Edits.Layout);
    }

    [Fact]
    public async Task VideoFrameExtractionRemembersTheSource()
    {
        string video = Path.Combine(_root, "clip.mp4");
        Directory.CreateDirectory(_root);
        await File.WriteAllBytesAsync(video, new byte[] { 0x00, 0x01 });

        UserSession loaded = await _service.SetImageFromVideoAsync(UserId, video, frame: 2, "clip");
        Assert.True(loaded.HasImage);
        Assert.NotNull(loaded.VideoSourcePath);
        Assert.Equal(2, _cli.LastRequest!.Frame);
        Assert.Equal(loaded.VideoSourcePath, _cli.LastRequest!.Input);

        await _service.ExtractFrameAsync(UserId, frame: 5);
        Assert.Equal(5, _cli.LastRequest!.Frame);
        UserSession after = await _store.GetAsync(UserId);
        Assert.Equal(loaded.VideoSourcePath, after.VideoSourcePath); // same video reused
    }

    [Fact]
    public async Task ExtractFrameWithoutAVideoThrows()
    {
        await _service.BlankAsync(UserId, new BlankSpec()); // an image, not a video
        await Assert.ThrowsAsync<InvalidOperationException>(() => _service.ExtractFrameAsync(UserId, 0));
    }

    [Theory]
    [InlineData("http://127.0.0.1/secret.png")] // SSRF to loopback
    [InlineData("ftp://example.com/a.png")]      // non-http scheme
    [InlineData("/etc/passwd.png")]              // bare local path (LFI)
    public async Task SetImageFromUrlRejectsUnsafeSourcesWithoutInvokingTheCli(string url)
    {
        await Assert.ThrowsAsync<InvalidOperationException>(
            () => _service.SetImageFromUrlAsync(UserId, url, "img"));
        Assert.Equal(0, _cli.EditCalls); // the CLI never fetched it
    }

    [Fact]
    public async Task ExportLayoutJsonEmitsExpectedFields()
    {
        UserSession session = new()
        {
            UserId = UserId,
            OriginalWidth = 320,
            OriginalHeight = 240,
            Edits = new EditState { Filter = "sepia" },
        };
        string json = _service.ExportLayoutJson(session);
        Assert.Contains("\"imageWidth\": 320", json);
        Assert.Contains("\"imageHeight\": 240", json);
        Assert.Contains("\"filter\": \"sepia\"", json);
        Assert.Contains("\"lines\"", json);
    }
}
