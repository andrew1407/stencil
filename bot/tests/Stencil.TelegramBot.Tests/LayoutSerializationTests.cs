using System.Text;
using System.Text.Json;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Serialization;

namespace Stencil.TelegramBot.Tests;

/// <summary>The layout round trip in one place: <see cref="StencilLayoutParser"/> takes the bytes an upload or /layout hands in, and <see cref="StencilJson"/> is the shared camelCase serializer every Stencil front-end keys off.</summary>
public sealed class LayoutSerializationTests
{
    [Fact]
    public void Should_Round_Trip_A_Layout_And_Use_Camel_Case_Keys()
    {
        StencilLayout layout = new()
        {
            ImageWidth = 1024,
            ImageHeight = 768,
            Filter = "sepia",
            Lines =
            [
                new LayoutLine
                {
                    Points = [new LayoutPoint(1, 2), new LayoutPoint(3, 4)],
                    Color = "#FF0000",
                },
            ],
        };

        string json = StencilJson.Serialize(layout);
        Assert.Contains("\"imageWidth\"", json);
        Assert.Contains("\"imageHeight\"", json);
        Assert.Contains("\"pointSize\"", json);
        Assert.Contains("\"fillColor\"", json);
        // Canonical filter key only (Phase 6): imageFilter written, legacy "filter" never.
        Assert.Contains("\"imageFilter\":\"sepia\"", json);
        Assert.DoesNotContain("\"filter\"", json);

        StencilLayout? round = StencilJson.FromElement<StencilLayout>(StencilJson.ToElement(layout));
        Assert.NotNull(round);
        Assert.Equal(1024, round!.ImageWidth);
        Assert.Equal("sepia", round.Filter);
        LayoutLine line = Assert.Single(round.Lines);
        // Per-line defaults survive the round trip.
        Assert.Equal(LayoutLine.DEFAULT_THICKNESS, line.Thickness);
        Assert.Equal(LayoutLine.DEFAULT_POINT_SIZE, line.PointSize);
        Assert.Equal(LayoutLine.DEFAULT_STYLE, line.Style);
        Assert.Equal(LayoutLine.DEFAULT_FILL_COLOR, line.FillColor);
        Assert.Equal(LayoutLine.DEFAULT_LOCKED, line.Locked);
        Assert.Equal(2, line.Points.Count);
        Assert.Equal(3, line.Points[1].X);
    }

    [Fact]
    public void Should_Read_Both_Filter_Keys_With_Canonical_Winning()
    {
        StencilLayout? canonical = JsonSerializer.Deserialize<StencilLayout>(
            "{\"imageFilter\":\"bw\",\"lines\":[]}", StencilJson.Options);
        Assert.Equal("bw", canonical!.Filter);

        StencilLayout? legacy = JsonSerializer.Deserialize<StencilLayout>(
            "{\"filter\":\"sepia\",\"lines\":[]}", StencilJson.Options);
        Assert.Equal("sepia", legacy!.Filter);

        // Both spellings, either order: the canonical imageFilter wins.
        StencilLayout? both = JsonSerializer.Deserialize<StencilLayout>(
            "{\"filter\":\"sepia\",\"imageFilter\":\"bw\",\"lines\":[]}", StencilJson.Options);
        Assert.Equal("bw", both!.Filter);
        StencilLayout? bothReversed = JsonSerializer.Deserialize<StencilLayout>(
            "{\"imageFilter\":\"bw\",\"filter\":\"sepia\",\"lines\":[]}", StencilJson.Options);
        Assert.Equal("bw", bothReversed!.Filter);
    }

    [Fact]
    public void Should_Serialize_A_Project_Record_In_Camel_Case()
    {
        ProjectRecord record = new()
        {
            Id = "p_1",
            Name = "Shot",
            CreatedAt = 111,
            UpdatedAt = 222,
            HasImage = true,
            ImageW = 64,
            ImageH = 48,
            Version = 7,
        };
        string json = StencilJson.Serialize(record);
        Assert.Contains("\"imageW\"", json);
        Assert.Contains("\"imageH\"", json);
        Assert.Contains("\"createdAt\"", json);
        Assert.Contains("\"version\"", json);

        ProjectRecord? round = StencilJson.FromElement<ProjectRecord>(StencilJson.ToElement(record));
        Assert.NotNull(round);
        Assert.Equal("p_1", round!.Id);
        Assert.Equal(64, round.ImageW);
        Assert.Equal(111, round.CreatedAt);
        Assert.Equal(7, round.Version);
    }

    [Fact]
    public void Should_Deserialize_A_Project_Record_From_Camel_Case_Json()
    {
        string wire = "{\"id\":\"p_9\",\"name\":\"A\",\"createdAt\":5,\"updatedAt\":6,\"hasImage\":true,\"imageW\":3,\"imageH\":4,\"version\":2}";
        ProjectRecord? record = JsonSerializer.Deserialize<ProjectRecord>(wire, StencilJson.Options);
        Assert.NotNull(record);
        Assert.Equal("p_9", record!.Id);
        Assert.Equal(3, record.ImageW);
        Assert.Equal(5, record.CreatedAt);
        Assert.Equal(2, record.Version);
    }

    [Fact]
    public void Should_Read_A_Valid_Layout_From_Bytes_In_The_Parser()
    {
        const string json = """
            {"imageWidth":800,"imageHeight":600,"lines":[
              {"points":[{"x":1,"y":2},{"x":3,"y":4}],"color":"#ff0000","thickness":2}
            ]}
            """;
        StencilLayout? layout = StencilLayoutParser.Parse(Encoding.UTF8.GetBytes(json));
        Assert.NotNull(layout);
        Assert.Equal(800, layout!.ImageWidth);
        Assert.Equal(600, layout.ImageHeight);
        Assert.Single(layout.Lines);
    }

    [Theory]
    [InlineData("not json at all")]
    [InlineData("{\"imageWidth\":")]
    [InlineData("")]
    public void Should_Yield_Null_Rather_Than_Throw_On_Malformed_Json_In_The_Parser(string body)
    {
        Assert.Null(StencilLayoutParser.Parse(Encoding.UTF8.GetBytes(body)));
    }
}
