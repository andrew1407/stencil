using System.Text;
using Stencil.TelegramBot.Domain.Layout;
using Xunit;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The shared layout-bytes parser behind both the .json document upload and /layout:
/// a valid layout parses, malformed JSON yields null (never throws).
/// </summary>
public class StencilLayoutParserTests
{
    [Fact]
    public void ParsesAValidLayout()
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
    public void MalformedJsonYieldsNull(string body)
    {
        Assert.Null(StencilLayoutParser.Parse(Encoding.UTF8.GetBytes(body)));
    }
}
