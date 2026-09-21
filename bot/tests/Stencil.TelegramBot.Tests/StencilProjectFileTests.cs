using System.Text;
using System.Text.Json;
using Stencil.TelegramBot.Domain.Project;
using Stencil.TelegramBot.Domain.Serialization;

namespace Stencil.TelegramBot.Tests;

/// <summary>The shared <c>.stencil</c> (de)serializer: a project round-trips through Build/Parse and foreign, malformed or too-new documents parse to null rather than throw. Cross-surface parity with browser project/file.js, the CLI project.zig and the desktop fileStore round-trip.</summary>
public class StencilProjectFileTests
{
    private static readonly byte[] _imageBytes = [0xDE, 0xAD, 0xBE, 0xEF];

    private static JsonElement layout() => StencilJson.ToElement(new
    {
        imageWidth = 4,
        imageHeight = 2,
        lines = new object[] { new { points = new[] { new { x = 0, y = 0 } }, color = "#ff0000" } },
        imageFilter = "bw",
        rotationQuarters = 1,
    });

    [Fact]
    public void Should_Round_Trip_Image_Layout_And_Metadata()
    {
        var project = new StencilProject
        {
            Name = "Red Dot",
            Color = "#7c3aed",
            Keywords = ["road", "sign"],
            Source = "https://example.com/a.png",
            ImageBytes = _imageBytes,
            ImageExt = "png",
            ImageWidth = 4,
            ImageHeight = 2,
            Layout = layout(),
        };

        byte[] bytes = Encoding.UTF8.GetBytes(StencilProjectFile.Build(project));
        StencilProject? parsed = StencilProjectFile.Parse(bytes);

        Assert.NotNull(parsed);
        Assert.Equal("Red Dot", parsed!.Name);
        Assert.Equal("#7c3aed", parsed.Color);
        Assert.Equal(new[] { "road", "sign" }, parsed.Keywords);
        Assert.Equal("https://example.com/a.png", parsed.Source);
        Assert.Equal(_imageBytes, parsed.ImageBytes);
        Assert.Equal(4, parsed.ImageWidth);
        Assert.Equal("png", parsed.ImageExt);
        Assert.NotNull(parsed.Layout);
        Assert.Equal("bw", parsed.Layout!.Value.GetProperty("imageFilter").GetString());
        Assert.Equal(1, parsed.Layout.Value.GetProperty("rotationQuarters").GetInt32());
    }

    [Fact]
    public void Should_Omit_Empty_Metadata_From_The_File()
    {
        string json = StencilProjectFile.Build(new StencilProject
        {
            Name = "Bare",
            ImageBytes = _imageBytes,
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
    public void Should_Reject_Foreign_Or_Malformed_Files(string json)
    {
        Assert.Null(StencilProjectFile.Parse(Encoding.UTF8.GetBytes(json)));
    }
}
