using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Tests.Doubles;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// What an edit accumulates on the session: blank, crop/rotate/filter, the page-format rules
/// and layout application (replace vs combine).
/// </summary>
public sealed class EditingServiceTests : EditingServiceTestBase
{
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

    // `combine` mirrors the GUI editors' Combine/Replace prompt and the CLI console's
    // `apply … combine`: keep what is drawn and add the incoming lines after it.
    private static StencilLayout layoutWith(int x) => new()
    {
        Lines = [new LayoutLine { Points = [new LayoutPoint(x, x), new LayoutPoint(x + 1, x + 1)] }],
    };

    [Fact]
    public async Task ApplyLayoutReplacesTheCurrentLinesByDefault()
    {
        await _service.BlankAsync(UserId, new BlankSpec());
        await _service.ApplyLayoutAsync(UserId, layoutWith(1));
        UserSession session = await _service.ApplyLayoutAsync(UserId, layoutWith(5));

        Assert.Single(session.Edits.Layout!.Lines);
        Assert.Equal(5, session.Edits.Layout!.Lines[0].Points[0].X);
    }

    [Fact]
    public async Task ApplyLayoutCombineKeepsTheExistingLinesAndAddsTheNewOnTop()
    {
        await _service.BlankAsync(UserId, new BlankSpec());
        await _service.ApplyLayoutAsync(UserId, layoutWith(1));
        UserSession session = await _service.ApplyLayoutAsync(UserId, layoutWith(5), combine: true);

        Assert.Equal(2, session.Edits.Layout!.Lines.Count);
        Assert.Equal(1, session.Edits.Layout!.Lines[0].Points[0].X);   // existing first…
        Assert.Equal(5, session.Edits.Layout!.Lines[1].Points[0].X);   // …new on top
    }

    [Fact]
    public async Task ApplyLayoutCombineOnAnEmptyDrawingJustAdoptsTheLayout()
    {
        await _service.BlankAsync(UserId, new BlankSpec());
        UserSession session = await _service.ApplyLayoutAsync(UserId, layoutWith(3), combine: true);

        Assert.Single(session.Edits.Layout!.Lines);
        Assert.Equal(3, session.Edits.Layout!.Lines[0].Points[0].X);
    }
}
