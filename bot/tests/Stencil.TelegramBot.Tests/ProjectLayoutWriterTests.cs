using System.Text.Json;
using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Layout;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// <see cref="ProjectLayoutWriter"/> — saving back a merged layout that keeps the project's
/// crop/page/formula fields while updating the bot-owned lines/filter/rotation.
/// </summary>
public sealed class ProjectLayoutWriterTests
{
    [Fact]
    public void PreservesCropAndPageWhileUpdatingBotFields()
    {
        // A fetched project layout the bot doesn't fully model (crop + page + formulas).
        string baseLayout = """
        {
          "imageWidth": 330, "imageHeight": 467,
          "imageFilter": "none", "rotationQuarters": 1,
          "cropRect": { "x": 0, "y": 16, "width": 330, "height": 467 },
          "pageSize": "A4", "allowFormulas": true, "formulaX": "x*2",
          "lines": []
        }
        """;
        var edits = new EditState
        {
            Rotate = 1,
            Filter = "#ff5623",
            Layout = new StencilLayout { Lines = [new LayoutLine { Points = [new LayoutPoint(1, 2)] }] },
        };

        var root = ProjectLayoutWriter.Build(baseLayout, edits, 330, 467);
        JsonElement el = JsonSerializer.Deserialize<JsonElement>(root.ToJsonString());

        // Preserved from the fetched layout:
        Assert.Equal("A4", el.GetProperty("pageSize").GetString());
        Assert.True(el.GetProperty("allowFormulas").GetBoolean());
        Assert.Equal("x*2", el.GetProperty("formulaX").GetString());
        Assert.Equal(330, el.GetProperty("cropRect").GetProperty("width").GetInt32());
        // Updated from the edit state:
        Assert.Equal("custom", el.GetProperty("imageFilter").GetString());
        Assert.Equal("#ff5623", el.GetProperty("filterColor").GetString());
        Assert.Equal(1, el.GetProperty("rotationQuarters").GetInt32());
        Assert.Equal(1, el.GetProperty("lines").GetArrayLength());
    }

    [Theory]
    [InlineData("invert")]
    [InlineData("contour")]
    public void InvertAndContourStayNamedFilters(string mode)
    {
        var edits = new EditState { Filter = mode };
        var root = ProjectLayoutWriter.Build(baseLayoutJson: null, edits, 100, 100);
        JsonElement el = JsonSerializer.Deserialize<JsonElement>(root.ToJsonString());

        Assert.Equal(mode, el.GetProperty("imageFilter").GetString()); // NOT coerced to custom
        Assert.False(el.TryGetProperty("filterColor", out _));
    }

    [Fact]
    public void PageFormatOverridesTheFetchedPageSize()
    {
        string baseLayout = """{ "pageSize": "A4", "lines": [] }""";
        var edits = new EditState { PageFormat = "B5" };
        var root = ProjectLayoutWriter.Build(baseLayout, edits, 100, 100);
        JsonElement el = JsonSerializer.Deserialize<JsonElement>(root.ToJsonString());

        Assert.Equal("B5", el.GetProperty("pageSize").GetString());
    }

    [Fact]
    public void CustomPageFormatCarriesItsCmDimensions()
    {
        var edits = new EditState { PageFormat = "custom", CustomPageWidth = 10, CustomPageHeight = 15.5 };
        var root = ProjectLayoutWriter.Build(baseLayoutJson: null, edits, 100, 100);
        JsonElement el = JsonSerializer.Deserialize<JsonElement>(root.ToJsonString());

        Assert.Equal("custom", el.GetProperty("pageSize").GetString());
        Assert.Equal(10, el.GetProperty("customPageWidth").GetDouble());
        Assert.Equal(15.5, el.GetProperty("customPageHeight").GetDouble());
    }

    [Fact]
    public void BuildsAFreshLayoutWhenNoBaseIsGiven()
    {
        var edits = new EditState { Filter = "bw" };
        var root = ProjectLayoutWriter.Build(baseLayoutJson: null, edits, 640, 480);
        JsonElement el = JsonSerializer.Deserialize<JsonElement>(root.ToJsonString());

        Assert.Equal("bw", el.GetProperty("imageFilter").GetString());
        Assert.Equal(0, el.GetProperty("rotationQuarters").GetInt32());
        Assert.Equal(640, el.GetProperty("imageWidth").GetInt32());
        Assert.Equal(0, el.GetProperty("lines").GetArrayLength());
    }
}
