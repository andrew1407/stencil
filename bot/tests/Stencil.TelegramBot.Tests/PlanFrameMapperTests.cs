using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Domain.Layout;

namespace Stencil.TelegramBot.Tests;

/// <summary>The snapshot-frame mapper (<c>llm-contract.md</c> §1): crop steps subtract the resolved origin, rotate steps turn points with the CW quarter mapping (x,y) → (h-y,x), and every mapped point clamps into the final tracked bounds.</summary>
public sealed class PlanFrameMapperTests
{
    [Fact]
    public void Should_Subtract_The_Resolved_Origin_For_A_Crop_Step()
    {
        PlanFrameMapper mapper = new(400, 300);
        mapper.RecordCrop("x1=100px");   // resolves to rect (100, 0, 300, 300)

        Assert.Equal(new LayoutPoint(50, 50), mapper.Map(new LayoutPoint(150, 50)));
        Assert.Equal(300, mapper.Width);
        Assert.Equal(300, mapper.Height);
    }

    [Fact]
    public void Should_Map_With_The_Pre_Rotate_Dims_For_Rotate_Right()
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
    public void Should_Equal_Three_Clockwise_Quarters_For_Rotate_Left()
    {
        PlanFrameMapper mapper = new(400, 300);
        mapper.RecordRotate(-1);

        // One CCW turn: (x, y) -> (y, w - x).
        Assert.Equal(new LayoutPoint(20, 390), mapper.Map(new LayoutPoint(10, 20)));
        Assert.Equal(300, mapper.Width);
        Assert.Equal(400, mapper.Height);
    }

    [Fact]
    public void Should_Be_A_Point_Reflection_When_Rotated_Twice()
    {
        PlanFrameMapper mapper = new(400, 300);
        mapper.RecordRotate(2);

        Assert.Equal(new LayoutPoint(390, 280), mapper.Map(new LayoutPoint(10, 20)));
        Assert.Equal(400, mapper.Width);
        Assert.Equal(300, mapper.Height);
    }

    [Fact]
    public void Should_Chain_Through_Both_Steps_For_Crop_Then_Rotate()
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
    public void Should_Clamp_Out_Of_Bounds_Points_Into_The_Final_Frame()
    {
        PlanFrameMapper mapper = new(400, 300);

        Assert.Equal(new LayoutPoint(0, 300), mapper.Map(new LayoutPoint(-5, 1000)));
        Assert.Equal(new LayoutPoint(400, 0), mapper.Map(new LayoutPoint(500, -1)));
    }

    [Fact]
    public void Should_Record_Nothing_For_An_Unresolvable_Crop()
    {
        PlanFrameMapper mapper = new(400, 300);
        mapper.RecordCrop("x1=abc");

        Assert.Equal(new LayoutPoint(10, 20), mapper.Map(new LayoutPoint(10, 20)));
        Assert.Equal(400, mapper.Width);
    }

    [Fact]
    public void Should_Drop_All_Steps_And_Restart_At_The_New_Frame_On_Reset()
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
    public void Should_Keep_Every_Non_Point_Field_Unchanged_In_Map_Lines()
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
