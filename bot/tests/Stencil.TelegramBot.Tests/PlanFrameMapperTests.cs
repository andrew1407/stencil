using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Domain.Layout;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The snapshot-frame mapper (<c>llm-contract.md</c> §1): crop steps subtract the resolved
/// origin, rotate steps turn points with the CW quarter mapping (x,y) → (h-y,x), and every
/// mapped point clamps into the final tracked bounds.
/// </summary>
public sealed class PlanFrameMapperTests
{
    [Fact]
    public void CropStepSubtractsTheResolvedOrigin()
    {
        PlanFrameMapper mapper = new(400, 300);
        mapper.RecordCrop("x1=100px");   // resolves to rect (100, 0, 300, 300)

        Assert.Equal(new LayoutPoint(50, 50), mapper.Map(new LayoutPoint(150, 50)));
        Assert.Equal(300, mapper.Width);
        Assert.Equal(300, mapper.Height);
    }

    [Fact]
    public void RotateRightMapsWithThePreRotateDims()
    {
        PlanFrameMapper mapper = new(400, 300);
        mapper.RecordRotate(1);

        // (x, y) -> (h - y, x) with (w,h) = (400,300); the frame becomes 300x400.
        Assert.Equal(new LayoutPoint(280, 10), mapper.Map(new LayoutPoint(10, 20)));
        Assert.Equal(300, mapper.Width);
        Assert.Equal(400, mapper.Height);
        // Marker consistency with the core's pixel mapping (h-1-y, x): the center of pixel
        // (0,0) must land on the center of pixel (h-1, 0).
        Assert.Equal(new LayoutPoint(299.5, 0.5), mapper.Map(new LayoutPoint(0.5, 0.5)));
    }

    [Fact]
    public void RotateLeftIsThreeClockwiseQuarters()
    {
        PlanFrameMapper mapper = new(400, 300);
        mapper.RecordRotate(-1);

        // One CCW turn: (x, y) -> (y, w - x).
        Assert.Equal(new LayoutPoint(20, 390), mapper.Map(new LayoutPoint(10, 20)));
        Assert.Equal(300, mapper.Width);
        Assert.Equal(400, mapper.Height);
    }

    [Fact]
    public void RotateTwiceIsAPointReflection()
    {
        PlanFrameMapper mapper = new(400, 300);
        mapper.RecordRotate(2);

        Assert.Equal(new LayoutPoint(390, 280), mapper.Map(new LayoutPoint(10, 20)));
        Assert.Equal(400, mapper.Width);
        Assert.Equal(300, mapper.Height);
    }

    [Fact]
    public void CropThenRotateChainsThroughBothSteps()
    {
        PlanFrameMapper mapper = new(400, 300);
        mapper.RecordCrop("x1=100px y1=50px");   // rect (100, 50, 300, 250)
        mapper.RecordRotate(1);                  // pre-rotate dims 300x250

        // (150,60) -crop-> (50,10) -CW-> (250-10, 50) = (240, 50).
        Assert.Equal(new LayoutPoint(240, 50), mapper.Map(new LayoutPoint(150, 60)));
        Assert.Equal(250, mapper.Width);
        Assert.Equal(300, mapper.Height);
    }

    [Fact]
    public void OutOfBoundsPointsClampIntoTheFinalFrame()
    {
        PlanFrameMapper mapper = new(400, 300);

        Assert.Equal(new LayoutPoint(0, 300), mapper.Map(new LayoutPoint(-5, 1000)));
        Assert.Equal(new LayoutPoint(400, 0), mapper.Map(new LayoutPoint(500, -1)));
    }

    [Fact]
    public void UnresolvableCropRecordsNothing()
    {
        PlanFrameMapper mapper = new(400, 300);
        mapper.RecordCrop("x1=abc");

        Assert.Equal(new LayoutPoint(10, 20), mapper.Map(new LayoutPoint(10, 20)));
        Assert.Equal(400, mapper.Width);
    }

    [Fact]
    public void ResetDropsAllStepsAndRestartsAtTheNewFrame()
    {
        PlanFrameMapper mapper = new(400, 300);
        mapper.RecordCrop("x1=100px");
        mapper.RecordRotate(1);
        mapper.Reset(640, 480);

        Assert.Equal(new LayoutPoint(10, 20), mapper.Map(new LayoutPoint(10, 20)));
        Assert.Equal(640, mapper.Width);
        Assert.Equal(480, mapper.Height);
    }

    [Fact]
    public void MapLinesKeepsEveryNonPointFieldUnchanged()
    {
        PlanFrameMapper mapper = new(400, 300);
        mapper.RecordCrop("x1=100px");
        LayoutLine line = new()
        {
            Points = [new LayoutPoint(150, 50), new LayoutPoint(90, 40)],
            Color = "#112233",
            Thickness = 5,
            Style = "dashed",
        };

        LayoutLine mapped = Assert.Single(mapper.MapLines([line]));

        Assert.Equal(new LayoutPoint(50, 50), mapped.Points[0]);
        Assert.Equal(new LayoutPoint(0, 40), mapped.Points[1]);   // -10 clamps to 0
        Assert.Equal("#112233", mapped.Color);
        Assert.Equal(5, mapped.Thickness);
        Assert.Equal("dashed", mapped.Style);
    }
}
