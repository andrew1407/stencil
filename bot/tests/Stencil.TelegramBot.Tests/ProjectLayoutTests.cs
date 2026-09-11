using System.Text.Json;
using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Layout;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The project-layout round trip, pinned as one because read and write are each other's
/// inverse: <see cref="ProjectLayoutMapper"/> rebuilds the edit state from a fetched layout
/// (lines · filter · rotation · rotated-space crop → CLI crop in original pixels) and
/// <see cref="ProjectLayoutWriter"/> saves it back, keeping the fields the bot doesn't model.
/// </summary>
public sealed class ProjectLayoutTests
{
    // The actual "cat" project: original 500x330, rotated one quarter, cropped in that
    // rotated space to 330x467, one yellow line, no filter.
    private const string CatLayout = """
    {
      "imageWidth": 330, "imageHeight": 467,
      "imageFilter": "none", "filterColor": "#7c3aed",
      "rotationQuarters": 1,
      "cropRect": { "x": 0, "y": 16, "width": 330, "height": 467 },
      "lines": [ { "color": "#FFFF00", "style": "solid", "locked": false,
                   "points": [ {"x": 238.3, "y": 165}, {"x": 71.3, "y": 206} ],
                   "fillColor": "transparent" } ]
    }
    """;

    private static JsonElement Parse(string json) => JsonDocument.Parse(json).RootElement;

    private static JsonElement Built(string? baseLayout, EditState edits, int w, int h) =>
        JsonSerializer.Deserialize<JsonElement>(
            ProjectLayoutWriter.Build(baseLayout, edits, w, h).ToJsonString());

    // ── reading a fetched layout ──

    [Fact]
    public void MapsRealRotatedAndCroppedProject()
    {
        EditState edits = ProjectLayoutMapper.ToEditState(Parse(CatLayout), 500, 330);

        Assert.Equal(1, edits.Rotate);
        Assert.Null(edits.Filter);                                  // imageFilter "none"
        Assert.Equal("x1=16px x2=483px y1=0px y2=329px", edits.CropSpec); // un-rotated to original space
        Assert.NotNull(edits.Layout);
        Assert.Single(edits.Layout!.Lines);
        Assert.Equal("#FFFF00", edits.Layout.Lines[0].Color);
    }

    [Fact]
    public void CustomFilterResolvesToTheTintColour()
    {
        var layout = Parse("""{ "imageFilter": "custom", "filterColor": "#ff5623", "lines": [] }""");
        Assert.Equal("#ff5623", ProjectLayoutMapper.ToEditState(layout, 100, 100).Filter);
    }

    [Theory]
    [InlineData("bw")]
    [InlineData("sepia")]
    [InlineData("invert")]
    [InlineData("contour")]
    public void NamedFiltersMapThrough(string mode)
    {
        var layout = Parse($$"""{ "imageFilter": "{{mode}}", "lines": [] }""");
        Assert.Equal(mode, ProjectLayoutMapper.ToEditState(layout, 100, 100).Filter);
    }

    [Fact]
    public void CropRectReadsCanonicalKeysAndCanonicalWins()
    {
        // Canonical browser form {x,y,w,h} reads.
        var canonical = Parse("""{ "cropRect": {"x":0,"y":16,"w":330,"h":467}, "rotationQuarters": 1, "lines": [] }""");
        Assert.Equal("x1=16px x2=483px y1=0px y2=329px",
            ProjectLayoutMapper.ToEditState(canonical, 500, 330).CropSpec);
        // Both forms present: canonical wins over the legacy width/height pair.
        var both = Parse("""{ "cropRect": {"x":0,"y":16,"w":330,"h":467,"width":1,"height":1}, "rotationQuarters": 1, "lines": [] }""");
        Assert.Equal("x1=16px x2=483px y1=0px y2=329px",
            ProjectLayoutMapper.ToEditState(both, 500, 330).CropSpec);
    }

    [Fact]
    public void FullCoverCropIsSkipped()
    {
        var layout = Parse("""{ "rotationQuarters": 0, "cropRect": {"x":0,"y":0,"width":100,"height":80}, "lines": [] }""");
        EditState edits = ProjectLayoutMapper.ToEditState(layout, 100, 80);
        Assert.Null(edits.CropSpec);   // covers the whole original — no crop
        Assert.Equal(0, edits.Rotate);
        Assert.Null(edits.Layout);     // no lines
    }

    // ── writing it back ──

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

        JsonElement el = Built(baseLayout, edits, 330, 467);

        Assert.Equal("A4", el.GetProperty("pageSize").GetString());
        Assert.True(el.GetProperty("allowFormulas").GetBoolean());
        Assert.Equal("x*2", el.GetProperty("formulaX").GetString());
        // A preserved legacy {width,height} cropRect is re-emitted with the canonical keys.
        Assert.Equal(330, el.GetProperty("cropRect").GetProperty("w").GetInt32());
        Assert.Equal(467, el.GetProperty("cropRect").GetProperty("h").GetInt32());
        Assert.False(el.GetProperty("cropRect").TryGetProperty("width", out _));
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
        JsonElement el = Built(null, new EditState { Filter = mode }, 100, 100);

        Assert.Equal(mode, el.GetProperty("imageFilter").GetString()); // NOT coerced to custom
        Assert.False(el.TryGetProperty("filterColor", out _));
    }

    [Fact]
    public void PageFormatOverridesTheFetchedPageSize()
    {
        JsonElement el = Built("""{ "pageSize": "A4", "lines": [] }""", new EditState { PageFormat = "B5" }, 100, 100);

        Assert.Equal("B5", el.GetProperty("pageSize").GetString());
    }

    [Fact]
    public void CustomPageFormatCarriesItsCmDimensions()
    {
        var edits = new EditState { PageFormat = "custom", CustomPageWidth = 10, CustomPageHeight = 15.5 };
        JsonElement el = Built(null, edits, 100, 100);

        Assert.Equal("custom", el.GetProperty("pageSize").GetString());
        Assert.Equal(10, el.GetProperty("customPageWidth").GetDouble());
        Assert.Equal(15.5, el.GetProperty("customPageHeight").GetDouble());
    }

    [Fact]
    public void BuildsAFreshLayoutWhenNoBaseIsGiven()
    {
        JsonElement el = Built(null, new EditState { Filter = "bw" }, 640, 480);

        Assert.Equal("bw", el.GetProperty("imageFilter").GetString());
        Assert.Equal(0, el.GetProperty("rotationQuarters").GetInt32());
        Assert.Equal(640, el.GetProperty("imageWidth").GetInt32());
        Assert.Equal(0, el.GetProperty("lines").GetArrayLength());
    }

    // ── the two together ──

    [Fact]
    public void WriteThenReadKeepsTheBotOwnedFields()
    {
        // Map the fetched project, write it straight back, and map it again: the fields the bot
        // owns (rotation, filter, crop, lines) must survive the round trip unchanged.
        EditState first = ProjectLayoutMapper.ToEditState(Parse(CatLayout), 500, 330);
        JsonElement written = Built(CatLayout, first, 330, 467);
        EditState second = ProjectLayoutMapper.ToEditState(written, 500, 330);

        Assert.Equal(first.Rotate, second.Rotate);
        Assert.Equal(first.Filter, second.Filter);
        Assert.Equal(first.CropSpec, second.CropSpec);
        Assert.Equal(first.Layout!.Lines.Count, second.Layout!.Lines.Count);
        Assert.Equal(first.Layout.Lines[0].Color, second.Layout.Lines[0].Color);
    }
}
