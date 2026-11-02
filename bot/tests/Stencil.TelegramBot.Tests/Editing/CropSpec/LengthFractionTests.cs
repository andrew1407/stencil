using Stencil.TelegramBot.Domain.Editing;

namespace Stencil.TelegramBot.Tests.Editing.CropSpec;

/// <summary>Lengths written N/D (the op registry's CROP_TOKEN grammar): the crop spec's unit and delta tokens, and the cm dims <c>/format custom</c> takes.</summary>
public sealed class LengthFractionTests
{
    [Theory]
    [InlineData("1/2in", 1.27)]
    [InlineData("25/2mm", 1.25)]
    [InlineData("21/2", 10.5)]
    [InlineData("21/2cm", 10.5)]
    [InlineData("1.5/.5", 3.0)]
    [InlineData("148mm", 14.8)]
    [InlineData("5IN", 12.7)]
    [InlineData(".5", 0.5)]
    [InlineData("29.7", 29.7)]
    public void Should_Parse_Decimal_And_Fraction_Cm(string text, double cm)
    {
        Assert.True(LengthNumber.TryParseCm(text, out double parsed));
        Assert.Equal(cm, parsed, 9);
    }

    [Theory]
    [InlineData("1/0in")]
    [InlineData("1/0.0")]
    [InlineData("1/in")]
    [InlineData("/2in")]
    [InlineData("1//2in")]
    [InlineData("1/2/3in")]
    [InlineData("1 /2")]
    [InlineData("1/ 2")]
    [InlineData("5.")]
    [InlineData("1/2px")]
    [InlineData("1/2%")]
    [InlineData("")]
    public void Should_Refuse_A_Malformed_Cm_Dim(string text)
    {
        Assert.False(LengthNumber.TryParseCm(text, out _));
    }

    [Fact]
    public void Should_Resolve_Fraction_Unit_Tokens_Through_The_Page_Metrics()
    {
        // 210x297 portrait → 10 px per cm: 1/2in = 12.7 px, 25/2mm = 12.5 px, 3/4in from the end = 210 - 19.05.
        Assert.Equal(new CropRect(13, 0, 197, 297),
            CropSpecResolver.Resolve("x1=1/2in y1=0 y2=297px", 210, 297, album: false));
        Assert.Equal(new CropRect(13, 0, 178, 297),
            CropSpecResolver.Resolve("x1=25/2mm x2=-3/4in y1=0 y2=297px", 210, 297, album: false));
        Assert.Equal(new CropRect(100, 0, 299, 300),
            CropSpecResolver.Resolve("x1=50/2% x2=-3/2px y1=0 y2=300px", 400, 300, album: false));
    }

    [Fact]
    public void Should_Treat_A_Bare_Fraction_As_A_Delta()
    {
        // x2 = 400 - 3/2 = 398.5, rounded like the CLI; x1 at 0 + 21/2 = 10.5.
        Assert.Equal(CropSpecResolver.Resolve("x1=10.5 x2=-1.5", 400, 300, album: false),
            CropSpecResolver.Resolve("x1=21/2 x2=-3/2", 400, 300, album: false));
        Assert.Equal(CropSpecResolver.Resolve("y2=-0.3333333333333333", 400, 300, album: false),
            CropSpecResolver.Resolve("y2=-1/3", 400, 300, album: false));
    }

    [Theory]
    [InlineData("x1=1/0in")]
    [InlineData("x1=1/0.0cm")]
    [InlineData("x1=1/in")]
    [InlineData("x1=/2in")]
    [InlineData("x1=1//2in")]
    [InlineData("x1=1/2/3in")]
    [InlineData("x1=1/2.in")]
    [InlineData("x1=-/2in")]
    [InlineData("x1=1/0")]
    public void Should_Refuse_A_Malformed_Fraction_Token(string spec)
    {
        Assert.Null(CropSpecResolver.Resolve(spec, 400, 300, album: false));
    }
}
