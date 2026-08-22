using Stencil.TelegramBot.Domain.Editing;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The C# port of the core's crop-spec resolution (<c>core/parse/cropSpec.cpp</c> +
/// <c>lengthTokens.cpp</c>, rounded/clamped like <c>cliApi.cpp stencil_cli_resolveCrop</c>),
/// with page metrics as A4 oriented to the image like the CLI pipeline derives them.
/// </summary>
public sealed class CropSpecResolverTests
{
    [Fact]
    public void SingleXAxisSpecDerivesTheHeightFromThePageAspectAndClampsIt()
    {
        // 400x300 landscape: A4 lies down (29.7x21), aspect = 21/29.7; the derived height
        // 300 / (21/29.7) ≈ 424 clamps to the image's 300.
        CropRect? rect = CropSpecResolver.Resolve("x1=100px", 400, 300, album: false);

        Assert.Equal(new CropRect(100, 0, 300, 300), rect);
    }

    [Fact]
    public void SingleYAxisSpecDerivesTheWidthFromThePageAspect()
    {
        // Portrait aspect 21/29.7: width = 100 * 21/29.7 ≈ 70.7 → 71.
        Assert.Equal(new CropRect(0, 0, 71, 100),
            CropSpecResolver.Resolve("y2=100px", 400, 300, album: false));
        // Album flips the proportion: 100 * 29.7/21 ≈ 141.4 → 141.
        Assert.Equal(new CropRect(0, 0, 141, 100),
            CropSpecResolver.Resolve("y2=100px", 400, 300, album: true));
    }

    [Fact]
    public void PercentTokensResolveAgainstEachAxisLength()
    {
        CropRect? rect = CropSpecResolver.Resolve("x1=25% x2=75% y1=25% y2=75%", 400, 300, album: false);

        Assert.Equal(new CropRect(100, 75, 200, 150), rect);
    }

    [Fact]
    public void MinusPrefixedUnitTokenMeasuresFromTheFarEnd()
    {
        CropRect? rect = CropSpecResolver.Resolve("x1=10px x2=-10px y1=0px y2=300px", 400, 300, album: false);

        Assert.Equal(new CropRect(10, 0, 380, 300), rect);
    }

    [Fact]
    public void BareNumberIsADeltaFromTheCurrentEdgeKeepingItsSign()
    {
        // x2/y2 default to the far edge, so "-20"/"-30" move them inward from there.
        Assert.Equal(new CropRect(0, 0, 380, 270),
            CropSpecResolver.Resolve("x2=-20 y2=-30", 400, 300, album: false));
        // x1 defaults to 0, so its "-20" lands at -20 and the rect clamps back to the image.
        Assert.Equal(new CropRect(0, 0, 400, 300),
            CropSpecResolver.Resolve("x1=-20 y1=0px y2=300px", 400, 300, album: false));
    }

    [Fact]
    public void CmMmAndInchTokensConvertThroughThePageMetrics()
    {
        // 210x297 portrait: A4 upright (21x29.7) → exactly 10 px per cm on both axes.
        Assert.Equal(new CropRect(10, 0, 190, 297),
            CropSpecResolver.Resolve("x1=1cm x2=20cm y1=0 y2=297px", 210, 297, album: false));
        Assert.Equal(new CropRect(10, 0, 200, 297),
            CropSpecResolver.Resolve("x1=10mm y1=0 y2=297px", 210, 297, album: false));
        Assert.Equal(new CropRect(25, 0, 185, 297),
            CropSpecResolver.Resolve("x1=1in y1=0 y2=297px", 210, 297, album: false));
    }

    [Fact]
    public void KeysAreCaseInsensitiveAndCommasSeparatePairs()
    {
        CropRect? rect = CropSpecResolver.Resolve("X1=10PX,Y1 = 20px", 400, 300, album: false);

        Assert.Equal(new CropRect(10, 20, 390, 280), rect);
    }

    [Fact]
    public void EmptySpecResolvesToTheFullImage()
    {
        Assert.Equal(new CropRect(0, 0, 400, 300), CropSpecResolver.Resolve("", 400, 300, album: false));
    }

    [Theory]
    [InlineData("x3=10")]                    // unknown key
    [InlineData("x1 10")]                    // missing '='
    [InlineData("x1=abc y1=0")]              // unparseable token
    [InlineData("x1=5. y1=0")]               // trailing dot is not a number
    [InlineData("x1=10px x2=10px y1=0 y2=300")]  // zero-width rect
    [InlineData("x1=400px")]                 // rect clamps to zero width
    public void MalformedOrEmptySpecsResolveToNull(string spec)
    {
        Assert.Null(CropSpecResolver.Resolve(spec, 400, 300, album: false));
    }

    // ── aspect (core/tests/cropSpec.test.cpp "aspect …" cases, lround-finished like the CLI) ──

    [Fact]
    public void AspectShrinksTheWidthAboutTheCentre()
    {
        // Edge rect 200x100 is too wide for 1:1 -> width shrinks to 100, centred at x=100.
        Assert.Equal(new CropRect(50, 0, 100, 100),
            CropSpecResolver.Resolve("x1 = 0px x2 = 200px y1 = 0px y2 = 100px aspect = 1:1", 400, 400, album: false));
    }

    [Fact]
    public void AspectShrinksTheHeightAboutTheCentre()
    {
        // The same 200x100 rect is too tall for 4:1 -> height shrinks to 50, centred at y=50.
        Assert.Equal(new CropRect(0, 25, 200, 50),
            CropSpecResolver.Resolve("x1 = 0px x2 = 200px y1 = 0px y2 = 100px aspect = 4:1", 400, 400, album: false));
    }

    [Fact]
    public void ExactFitAspectIsANoOpAndTheKeyIsCaseInsensitive()
    {
        Assert.Equal(new CropRect(10, 20, 200, 100),
            CropSpecResolver.Resolve("x1=10px x2=210px y1=20px y2=120px aspect=2:1", 400, 400, album: false));
        Assert.Equal(new CropRect(10, 20, 200, 100),
            CropSpecResolver.Resolve("x1=10px x2=210px y1=20px y2=120px, ASPECT=2:1", 400, 400, album: false));
    }

    [Fact]
    public void AspectAloneAppliesToTheFullImage()
    {
        Assert.Equal(new CropRect(80, 0, 480, 480),
            CropSpecResolver.Resolve("aspect=1:1", 640, 480, album: false));
        // Already at the ratio -> the whole image, untouched.
        Assert.Equal(new CropRect(0, 0, 640, 480),
            CropSpecResolver.Resolve("aspect=4:3", 640, 480, album: false));
    }

    [Fact]
    public void AspectKeepsFractionalCentresUntilTheFinalRounding()
    {
        // 101x100 rect, 1:1 -> width 100 and x moves by half a pixel to 0.5; the core hands
        // that on unrounded and the final lround (half away from zero) lands it at 1.
        Assert.Equal(new CropRect(1, 0, 100, 100),
            CropSpecResolver.Resolve("x1 = 0px x2 = 101px y1 = 0px y2 = 100px aspect = 1:1", 400, 400, album: false));
    }

    [Fact]
    public void DegenerateAspectResultClampsToOnePixelKeepingTheCentre()
    {
        // Height 0.1px floors to 1px, never grows past the rect; centre y=49.5 rounds to 50.
        Assert.Equal(new CropRect(0, 50, 100, 1),
            CropSpecResolver.Resolve("x1 = 0px x2 = 100px y1 = 0px y2 = 100px aspect = 1000:1", 400, 400, album: false));
    }

    [Theory]
    [InlineData("0:3")]      // zero width
    [InlineData("4:0")]      // zero height
    [InlineData("-4:3")]     // signed
    [InlineData("4:-3")]
    [InlineData("4")]        // no colon
    [InlineData("4:")]       // missing part
    [InlineData(":3")]
    [InlineData("4:3:2")]    // extra colon
    [InlineData("a:b")]      // not digits
    [InlineData("4.5:3")]    // not integers
    [InlineData("1e2:3")]    // exponent
    public void InvalidAspectStringsFailResolutionLikeInvalidTokens(string aspect)
    {
        Assert.Null(CropSpecResolver.Resolve($"aspect = {aspect}", 640, 480, album: false));
    }

    [Fact]
    public void AspectWithAMissingValueIsAStructuralParseError()
    {
        Assert.Null(CropSpecResolver.Resolve("aspect = ", 640, 480, album: false));
    }
}
