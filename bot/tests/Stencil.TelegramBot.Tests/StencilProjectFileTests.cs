using System.Text;
using System.Text.Json;
using Stencil.TelegramBot.Domain.Project;
using Stencil.TelegramBot.Domain.Serialization;
using Xunit;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The shared <c>.stencil</c> project-file (de)serializer: a project round-trips through
/// Build/Parse (image bytes survive base64, layout + metadata preserved), and foreign /
/// malformed / too-new documents parse to null (never throw). Cross-surface parity with
/// browser projectFile.js, the CLI project.zig, and the desktop fileStore round-trip.
/// </summary>
public class StencilProjectFileTests
{
    private static readonly byte[] ImageBytes = [0xDE, 0xAD, 0xBE, 0xEF];

    private static JsonElement Layout() => StencilJson.ToElement(new
    {
        imageWidth = 4,
        imageHeight = 2,
        lines = new object[] { new { points = new[] { new { x = 0, y = 0 } }, color = "#ff0000" } },
        imageFilter = "bw",
        rotationQuarters = 1,
    });

    [Fact]
    public void RoundTripsImageLayoutAndMetadata()
    {
        var project = new StencilProject
        {
            Name = "Red Dot",
            Color = "#7c3aed",
            Keywords = ["road", "sign"],
            Source = "https://example.com/a.png",
            ImageBytes = ImageBytes,
            ImageExt = "png",
            ImageWidth = 4,
            ImageHeight = 2,
            Layout = Layout(),
        };

        byte[] bytes = Encoding.UTF8.GetBytes(StencilProjectFile.Build(project));
        StencilProject? parsed = StencilProjectFile.Parse(bytes);

        Assert.NotNull(parsed);
        Assert.Equal("Red Dot", parsed!.Name);
        Assert.Equal("#7c3aed", parsed.Color);
        Assert.Equal(new[] { "road", "sign" }, parsed.Keywords);
        Assert.Equal("https://example.com/a.png", parsed.Source);
        Assert.Equal(ImageBytes, parsed.ImageBytes);
        Assert.Equal(4, parsed.ImageWidth);
        Assert.Equal("png", parsed.ImageExt);
        Assert.NotNull(parsed.Layout);
        Assert.Equal("bw", parsed.Layout!.Value.GetProperty("imageFilter").GetString());
        Assert.Equal(1, parsed.Layout.Value.GetProperty("rotationQuarters").GetInt32());
    }

    [Fact]
    public void OmitsEmptyMetadataFromTheFile()
    {
        string json = StencilProjectFile.Build(new StencilProject
        {
            Name = "Bare",
            ImageBytes = ImageBytes,
            ImageWidth = 1,
            ImageHeight = 1,
        });
        Assert.DoesNotContain("\"color\"", json);
        Assert.DoesNotContain("\"keywords\"", json);
        Assert.DoesNotContain("\"blank\"", json);
        Assert.Contains("\"format\": \"stencil-project\"", json);
    }

    [Theory]
    [InlineData("{\"version\":1}")]                                                        // no format
    [InlineData("{ not json")]                                                             // bad JSON
    [InlineData("{\"format\":\"stencil-project\",\"version\":999,\"image\":{\"dataUrl\":\"data:image/png;base64,AAAA\"}}")] // too new
    [InlineData("{\"format\":\"stencil-project\",\"version\":1}")]                          // no image
    public void RejectsForeignOrMalformed(string json)
    {
        Assert.Null(StencilProjectFile.Parse(Encoding.UTF8.GetBytes(json)));
    }
}
