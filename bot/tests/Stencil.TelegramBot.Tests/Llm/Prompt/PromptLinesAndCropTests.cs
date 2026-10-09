using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Serialization;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Tests.Llm.Prompt;

/// <summary>llm-contract §2 on the bot's session: a <c>layout</c> sets the drawn lines to exactly its own, an empty one clearing them, a <c>crop</c> cuts the view it is given and composes onto the stored crop, and the lines follow the crop as every editor's do.</summary>
public sealed class PromptLinesAndCropTests : PromptServiceTestBase
{
    public PromptLinesAndCropTests(PromptServiceFixture fixture) : base(fixture) { }

    private Task drawn() =>
        _editing.AddLineAsync(UserId, [new LayoutPoint(0, 0), new LayoutPoint(640, 480)], closed: false);

    [Fact]
    public async Task Should_Replace_The_Drawn_Lines_With_Each_Layouts_Own_As_One_Entry_Each()
    {
        await SeedImage();   // 640x480
        await drawn();
        int before = (await _store.GetAsync(UserId)).EditHistory.Count;
        Reply("""{"reply":"ok","actions":[{"op":"layout","lines":[{"points":[{"x":5,"y":6}]}]},{"op":"layout","lines":[{"points":[{"x":7,"y":8}]}]}]}""");

        await Prompt("add two marks");

        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal(new LayoutPoint(7, 8), Assert.Single(session.Edits.Layout!.Lines).Points[0]);
        Assert.Equal(before + 2, session.EditHistory.Count);
    }

    [Fact]
    public async Task Should_Clear_Every_Line_On_An_Empty_Layout_As_One_History_Entry()
    {
        await SeedImage();
        await drawn();
        int before = (await _store.GetAsync(UserId)).EditHistory.Count;
        Reply("""{"reply":"cleared","actions":[{"op":"layout","lines":[]}]}""");

        await Prompt("clear the lines");

        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal(0, session.Edits.LineCount);
        Assert.Equal(before + 1, session.EditHistory.Count);
    }

    [Fact]
    public async Task Should_Carry_A_Lines_Point_Colour_Into_The_Session_And_The_Render()
    {
        await SeedImage();
        Reply("""{"reply":"ok","actions":[{"op":"layout","lines":[{"points":[{"x":1,"y":2},{"x":3,"y":4}],"pointColor":"#ff0000"}]}]}""");

        await Prompt("mark it with red points");

        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal("#ff0000", Assert.Single(session.Edits.Layout!.Lines).PointColor);
        await _editing.RenderAsync(UserId);
        StencilLayout rendered = System.Text.Json.JsonSerializer.Deserialize<StencilLayout>(
            _cli.LayoutJson(_cli.LastRequest!)!, StencilJson.Options)!;
        Assert.Equal("#ff0000", Assert.Single(rendered.Lines).PointColor);
    }

    [Fact]
    public async Task Should_Compose_A_Crop_Onto_The_Stored_One()
    {
        await SeedImage();
        await _editing.SetCropAsync(UserId, "x1=100px x2=500px y1=40px y2=440px", album: false);
        Reply("""{"reply":"ok","actions":[{"op":"crop","spec":{"x1":"25%","x2":"-25%","y1":"25%","y2":"-25%"}}]}""");

        await Prompt("crop the middle");

        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal("x1=200px x2=400px y1=140px y2=340px", session.Edits.CropSpec);
    }

    [Fact]
    public async Task Should_Scale_The_Drawn_Lines_By_The_Width_Ratio_On_A_Crop()
    {
        await SeedImage();
        await drawn();
        Reply("""{"reply":"ok","actions":[{"op":"crop","spec":{"x1":"0px","x2":"320px","y1":"0px","y2":"240px"}}]}""");

        await Prompt("crop the top-left quarter");

        LayoutLine line = Assert.Single((await _store.GetAsync(UserId)).Edits.Layout!.Lines);
        Assert.Equal([new LayoutPoint(0, 0), new LayoutPoint(320, 240)], line.Points);
    }

    [Fact]
    public async Task Should_Clear_The_Drawn_Lines_On_A_Crop_That_Turns_The_Picture_Portrait()
    {
        await SeedImage();
        await drawn();
        Reply("""{"reply":"ok","actions":[{"op":"crop","spec":{"x1":"0px","x2":"200px","y1":"0px","y2":"300px"}}]}""");

        await Prompt("crop a portrait strip");

        Assert.Equal(0, (await _store.GetAsync(UserId)).Edits.LineCount);
    }

    [Fact]
    public async Task Should_Replace_The_Drawn_Lines_With_A_Variants_Layout()
    {
        await SeedImage();
        await drawn();
        Reply("""{"reply":"v","actions":[],"variants":[{"label":"more","actions":[{"op":"layout","lines":[{"points":[{"x":5,"y":6}]}]}]}]}""");

        await Prompt("one take with another mark");

        StencilLayout rendered = System.Text.Json.JsonSerializer.Deserialize<StencilLayout>(
            _cli.LayoutJson(_cli.LastRequest!)!, StencilJson.Options)!;
        Assert.Equal(new LayoutPoint(5, 6), Assert.Single(rendered.Lines).Points[0]);
        Assert.Equal(1, (await _store.GetAsync(UserId)).Edits.LineCount);   // variants never mutate
    }
}
