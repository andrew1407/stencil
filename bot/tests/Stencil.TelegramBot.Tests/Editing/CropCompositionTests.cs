using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Layout;

namespace Stencil.TelegramBot.Tests.Editing;

/// <summary>A plan crop cuts the VIEW — the stored crop turned by the edit's quarter turns — and lands as one px window over the unturned original, the lines following <c>core::cropChange</c> (the CLI console's applyCrop, the editors' crop over a crop).</summary>
public sealed class CropCompositionTests
{
    private static StencilLayout lined(params LayoutPoint[] points) =>
        new() { Lines = [new LayoutLine { Points = points, PointColor = "#ff0000" }] };

    [Fact]
    public void Should_Keep_The_Spec_As_Written_When_The_View_Is_The_Original()
    {
        CropComposition c = CropSpecResolver.Compose("x1=10% x2=-10%", null, false, 0, 640, 480)!;

        Assert.Equal("x1=10% x2=-10%", c.Spec);
        Assert.Equal((640, 480), (c.ViewWidth, c.ViewHeight));
    }

    [Fact]
    public void Should_Resolve_The_Spec_Inside_The_Stored_Crop_And_Store_The_Composition()
    {
        CropComposition c = CropSpecResolver.Compose(
            "x1=25% x2=-25% y1=25% y2=-25%", "x1=100px x2=500px y1=40px y2=440px", false, 0, 640, 480)!;

        Assert.Equal("x1=200px x2=400px y1=140px y2=340px", c.Spec);
        Assert.Equal(new CropRect(100, 100, 200, 200), c.Window);
        Assert.Equal(new CropRect(200, 140, 200, 200), CropSpecResolver.Resolve(c.Spec, 640, 480, false));
    }

    [Fact]
    public void Should_Map_A_Window_Of_The_Turned_View_Back_Onto_The_Unturned_Original()
    {
        // One clockwise quarter: the 480x640 view's top-left 100x50 is the original's bottom-left 50x100.
        CropComposition c = CropSpecResolver.Compose("x1=0px x2=100px y1=0px y2=50px", null, false, 1, 640, 480)!;

        Assert.Equal("x1=0px x2=50px y1=380px y2=480px", c.Spec);
        Assert.Equal((480, 640), (c.ViewWidth, c.ViewHeight));
    }

    [Theory]
    [InlineData(1)]
    [InlineData(2)]
    [InlineData(3)]
    public void Should_Turn_The_Composed_Window_Back_Into_Exactly_The_View_Window(int quarters)
    {
        const string stored = "x1=40px x2=600px y1=20px y2=420px";   // a 560x400 window
        CropComposition c = CropSpecResolver.Compose("x1=30px x2=110px y1=7px y2=57px", stored, false, quarters, 640, 480)!;

        CropRect composed = CropSpecResolver.Resolve(c.Spec, 640, 480, false)!;
        CropRect local = composed with { X = composed.X - 40, Y = composed.Y - 20 };
        Assert.Equal(c.Window, turn(local, quarters, 560, 400));
    }

    // Forward: q clockwise quarters of a rect inside a w x h frame, (x, y) -> (h - y, x) each.
    private static CropRect turn(CropRect r, int q, int w, int h)
    {
        for (int i = 0; i < q; i++)
        {
            r = new CropRect(h - r.Y - r.Height, r.X, r.Height, r.Width);
            (w, h) = (h, w);
        }
        return r;
    }

    [Fact]
    public void Should_Scale_The_Lines_By_The_Width_Ratio_On_A_Crop_That_Keeps_The_Orientation()
    {
        StencilLayout after = lined(new LayoutPoint(100, 40), new LayoutPoint(640, 480)).Recropped(640, 480, 320, 240);

        Assert.Equal([new LayoutPoint(50, 20), new LayoutPoint(320, 240)], after.Lines[0].Points);
        Assert.Equal("#ff0000", after.Lines[0].PointColor);
    }

    [Theory]
    [InlineData(200, 300)]   // album to portrait
    [InlineData(300, 300)]   // a square is portrait: width > height is album
    public void Should_Clear_The_Lines_When_The_Crop_Flips_The_Orientation(int newW, int newH)
    {
        Assert.Empty(lined(new LayoutPoint(1, 1)).Recropped(640, 480, newW, newH).Lines);
    }

    [Fact]
    public void Should_Compose_The_Crop_And_Rescale_The_Lines_As_One_State()
    {
        EditState edits = new() { CropSpec = "x1=0px x2=320px y1=0px y2=240px", Layout = lined(new LayoutPoint(160, 120)) };

        EditState after = edits.WithViewCrop("x1=0px x2=160px y1=0px y2=120px", 640, 480);

        Assert.Equal("x1=0px x2=160px y1=0px y2=120px", after.CropSpec);
        Assert.Equal(new LayoutPoint(80, 60), after.Layout!.Lines[0].Points[0]);
        Assert.False(after.Album);
    }

    [Fact]
    public void Should_Store_A_Spec_That_Resolves_To_Nothing_As_Sent_For_The_Render_To_Report()
    {
        EditState edits = new() { Layout = lined(new LayoutPoint(1, 1)) };

        EditState after = edits.WithViewCrop("x1=100% x2=100%", 640, 480);

        Assert.Equal("x1=100% x2=100%", after.CropSpec);
        Assert.Single(after.Layout!.Lines);
    }
}
