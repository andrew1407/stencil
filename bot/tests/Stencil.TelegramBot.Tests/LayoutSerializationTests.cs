using System.Text.Json;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Serialization;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// camelCase wire-shape parity for the layout and project DTOs via <see cref="StencilJson"/> —
/// the shared serializer every Stencil front-end keys off (<c>imageWidth</c>, <c>markerSize</c>,
/// <c>fillColor</c>, <c>imageW</c>, <c>createdAt</c>, <c>version</c>).
/// </summary>
public sealed class LayoutSerializationTests
{
    [Fact]
    public void LayoutRoundTripsAndUsesCamelCaseKeys()
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
        Assert.Contains("\"markerSize\"", json);
        Assert.Contains("\"fillColor\"", json);

        StencilLayout? round = StencilJson.FromElement<StencilLayout>(StencilJson.ToElement(layout));
        Assert.NotNull(round);
        Assert.Equal(1024, round!.ImageWidth);
        Assert.Equal("sepia", round.Filter);
        LayoutLine line = Assert.Single(round.Lines);
        // Per-line defaults survive the round trip.
        Assert.Equal(LayoutLine.DefaultThickness, line.Thickness);
        Assert.Equal(LayoutLine.DefaultMarkerSize, line.MarkerSize);
        Assert.Equal(LayoutLine.DefaultStyle, line.Style);
        Assert.Equal(LayoutLine.DefaultFillColor, line.FillColor);
        Assert.Equal(LayoutLine.DefaultLocked, line.Locked);
        Assert.Equal(2, line.Points.Count);
        Assert.Equal(3, line.Points[1].X);
    }

    [Fact]
    public void ProjectRecordSerializesCamelCase()
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
    public void ProjectRecordDeserializesFromCamelCaseJson()
    {
        string wire = "{\"id\":\"p_9\",\"name\":\"A\",\"createdAt\":5,\"updatedAt\":6,\"hasImage\":true,\"imageW\":3,\"imageH\":4,\"version\":2}";
        ProjectRecord? record = JsonSerializer.Deserialize<ProjectRecord>(wire, StencilJson.Options);
        Assert.NotNull(record);
        Assert.Equal("p_9", record!.Id);
        Assert.Equal(3, record.ImageW);
        Assert.Equal(5, record.CreatedAt);
        Assert.Equal(2, record.Version);
    }
}
