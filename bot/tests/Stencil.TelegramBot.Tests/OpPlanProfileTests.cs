using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Application.Llm.Plan;

namespace Stencil.TelegramBot.Tests;

/// <summary>The rest of the §10 bot profile: pen fields, http(s)-only <c>openUrl</c>, the bounded name/description, the two colours and <c>export</c>'s two values.</summary>
public sealed class OpPlanProfileTests
{
    [Fact]
    public void Should_Accept_Any_Subset_Of_The_Pen_Fields_For_Line_Style()
    {
        OpPlanParseResult result = OpPlanParser.Parse(
            """
            {"reply":"ok","actions":[{"op":"lineStyle","color":"#00ff00","thickness":3,
              "pointSize":6,"style":"dashed","fillColor":"transparent","pointColor":"","drawMode":"rect"}]}
            """);

        Assert.Null(result.Error);
        LineStyleAction pen = Assert.IsType<LineStyleAction>(Assert.Single(result.Plan!.Actions));
        Assert.Equal("#00ff00", pen.Color);
        Assert.Equal(3, pen.Thickness);
        Assert.Equal(6, pen.PointSize);
        Assert.Equal("dashed", pen.Style);
        Assert.Equal("transparent", pen.FillColor);
        Assert.Equal("", pen.PointColor);
        Assert.Equal("rect", pen.DrawMode);

        // A CSS colour name works like the /color command's.
        OpPlanParseResult named = OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"lineStyle","color":"tomato"}]}""");
        Assert.Null(named.Error);
        Assert.Equal("tomato", Assert.IsType<LineStyleAction>(named.Plan!.Actions[0]).Color);
    }

    [Theory]
    [InlineData("""{"op":"lineStyle"}""")]                          // at least one field
    [InlineData("""{"op":"lineStyle","thickness":0}""")]            // 1..20
    [InlineData("""{"op":"lineStyle","thickness":21}""")]
    [InlineData("""{"op":"lineStyle","pointSize":31}""")]           // 1..30
    [InlineData("""{"op":"lineStyle","style":"wavy"}""")]
    [InlineData("""{"op":"lineStyle","drawMode":"circle"}""")]
    [InlineData("""{"op":"lineStyle","fillColor":"red"}""")]        // hex or "transparent" only
    [InlineData("""{"op":"lineStyle","color":"#12"}""")]
    [InlineData("""{"op":"lineStyle","pointColor":"red"}""")]       // hex or "" only
    [InlineData("""{"op":"lineStyle","width":2}""")]                // unknown field
    public void Should_Reject_Bad_Fields_For_Line_Style(string action)
    {
        OpPlanParseResult result = OpPlanParser.Parse($$"""{"reply":"x","actions":[{{action}}]}""");
        Assert.Null(result.Plan);
        Assert.Contains("lineStyle", result.Error);
    }

    // ── §10 openUrl ──

    [Fact]
    public void Should_Parse_A_Trimmed_Http_Url_With_An_Optional_Incognito_For_Open_Url()
    {
        OpPlanParseResult result = OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"openUrl","url":" https://pics.example/cat.png ","incognito":true}]}""");

        Assert.Null(result.Error);
        OpenUrlAction open = Assert.IsType<OpenUrlAction>(Assert.Single(result.Plan!.Actions));
        Assert.Equal("https://pics.example/cat.png", open.Url);
        Assert.True(open.Incognito);
    }

    [Theory]
    [InlineData("""{"op":"openUrl"}""")]
    [InlineData("""{"op":"openUrl","url":"ftp://x/a.png"}""")]
    [InlineData("""{"op":"openUrl","url":"not a url"}""")]
    [InlineData("""{"op":"openUrl","url":"http://"}""")]
    [InlineData("""{"op":"openUrl","url":"http://a b/c.png"}""")]
    [InlineData("""{"op":"openUrl","url":"https://x/a.png","incognito":"yes"}""")]
    [InlineData("""{"op":"openUrl","url":"https://x/a.png","cookies":true}""")]
    public void Should_Reject_Non_Http_Shapes_For_Open_Url(string action)
    {
        OpPlanParseResult result = OpPlanParser.Parse($$"""{"reply":"x","actions":[{{action}}]}""");
        Assert.Null(result.Plan);
        Assert.Contains("openUrl", result.Error);
    }

    // ── §10 renameProject / describe / blankColor / projectColor / export ──

    [Fact]
    public void Should_Trim_And_Bound_The_Name_For_Rename_Project()
    {
        OpPlanParseResult ok = OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"renameProject","name":" Poster draft "}]}""");
        Assert.Null(ok.Error);
        Assert.Equal("Poster draft", Assert.IsType<RenameProjectAction>(ok.Plan!.Actions[0]).Name);

        Assert.NotNull(OpPlanParser.Parse(
            """{"reply":"x","actions":[{"op":"renameProject","name":"   "}]}""").Error);
        string over = new('x', 81);
        Assert.NotNull(OpPlanParser.Parse(
            $$"""{"reply":"x","actions":[{"op":"renameProject","name":"{{over}}"}]}""").Error);
    }

    [Fact]
    public void Should_Take_Up_To_500_Chars_And_Clear_On_Empty_For_Describe()
    {
        OpPlanParseResult empty = OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"describe","text":""}]}""");
        Assert.Null(empty.Error);
        Assert.Equal("", Assert.IsType<DescribeAction>(empty.Plan!.Actions[0]).Text);

        string max = new('d', 500);
        Assert.Null(OpPlanParser.Parse(
            $$"""{"reply":"ok","actions":[{"op":"describe","text":"{{max}}"}]}""").Error);
        string over = new('d', 501);
        OpPlanParseResult tooLong = OpPlanParser.Parse(
            $$"""{"reply":"x","actions":[{"op":"describe","text":"{{over}}"}]}""");
        Assert.Null(tooLong.Plan);
        Assert.Contains("500", tooLong.Error);
        Assert.NotNull(OpPlanParser.Parse("""{"reply":"x","actions":[{"op":"describe"}]}""").Error);
    }

    [Fact]
    public void Should_Validate_The_Colours_For_Blank_Color_And_Project_Color()
    {
        OpPlanParseResult ok = OpPlanParser.Parse(
            """
            {"reply":"ok","actions":[{"op":"blankColor","color":"lavender"},
              {"op":"projectColor","color":"#ec4899"},{"op":"projectColor","color":""}]}
            """);
        Assert.Null(ok.Error);
        Assert.Equal("lavender", Assert.IsType<BlankColorAction>(ok.Plan!.Actions[0]).Color);
        Assert.Equal("#ec4899", Assert.IsType<ProjectColorAction>(ok.Plan.Actions[1]).Color);
        Assert.Equal("", Assert.IsType<ProjectColorAction>(ok.Plan.Actions[2]).Color);

        // blankColor takes hex/CSS names; projectColor only hex or "" (the clear).
        Assert.NotNull(OpPlanParser.Parse("""{"reply":"x","actions":[{"op":"blankColor","color":"#12345"}]}""").Error);
        Assert.NotNull(OpPlanParser.Parse("""{"reply":"x","actions":[{"op":"blankColor"}]}""").Error);
        Assert.NotNull(OpPlanParser.Parse("""{"reply":"x","actions":[{"op":"projectColor","color":"pink"}]}""").Error);
    }

    [Fact]
    public void Should_Take_Layout_Or_Project_Only_For_Export()
    {
        OpPlanParseResult ok = OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"export","what":"layout"},{"op":"export","what":"project"}]}""");
        Assert.Null(ok.Error);
        Assert.Equal("layout", Assert.IsType<ExportAction>(ok.Plan!.Actions[0]).What);
        Assert.Equal("project", Assert.IsType<ExportAction>(ok.Plan.Actions[1]).What);

        Assert.NotNull(OpPlanParser.Parse("""{"reply":"x","actions":[{"op":"export","what":"video"}]}""").Error);
        Assert.NotNull(OpPlanParser.Parse("""{"reply":"x","actions":[{"op":"export"}]}""").Error);
    }

    // ── §2 widened forms: page custom dims, blank cm dims, formula enabled/empty ──
}
