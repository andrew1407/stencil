using System.Text.Json;
using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Editing;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// <see cref="ProjectLayoutMapper"/> — reconstructing a fetched project's edit state from its
/// stored layout (lines · filter · rotation · rotated-space crop → CLI crop in original pixels).
/// </summary>
public sealed class ProjectLayoutMapperTests
{
    private static JsonElement Parse(string json) => JsonDocument.Parse(json).RootElement;

    [Fact]
    public void MapsRealRotatedAndCroppedProject()
    {
        // The actual "cat" project: original 500x330, rotated one quarter, cropped in that
        // rotated space to 330x467, one yellow line, no filter.
        var layout = Parse("""
        {
          "imageWidth": 330, "imageHeight": 467,
          "imageFilter": "none", "filterColor": "#7c3aed",
          "rotationQuarters": 1,
          "cropRect": { "x": 0, "y": 16, "width": 330, "height": 467 },
          "lines": [ { "color": "#FFFF00", "style": "solid", "locked": false,
                       "points": [ {"x": 238.3, "y": 165}, {"x": 71.3, "y": 206} ],
                       "fillColor": "transparent" } ]
        }
        """);

        EditState edits = ProjectLayoutMapper.ToEditState(layout, 500, 330);

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
        EditState edits = ProjectLayoutMapper.ToEditState(layout, 100, 100);
        Assert.Equal("#ff5623", edits.Filter);
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
    public void FullCoverCropIsSkipped()
    {
        var layout = Parse("""{ "rotationQuarters": 0, "cropRect": {"x":0,"y":0,"width":100,"height":80}, "lines": [] }""");
        EditState edits = ProjectLayoutMapper.ToEditState(layout, 100, 80);
        Assert.Null(edits.CropSpec);   // covers the whole original — no crop
        Assert.Equal(0, edits.Rotate);
        Assert.Null(edits.Layout);     // no lines
    }
}
