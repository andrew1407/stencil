using Stencil.TelegramBot.Infrastructure.Links;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The outbound desktop hand-off links. GOLDEN VECTORS — the scheme URL matches the
/// extension's <c>buildStencilSchemeUrl</c> server case (<c>extension/tests/openIn.test.js</c>)
/// and the bounce URL the browser's <c>buildDesktopBounceUrl</c>
/// (<c>browser/tests/deepLink.test.js</c>). Keep them in sync.
/// </summary>
public sealed class DesktopLinkBuilderTests
{
    [Fact]
    public void SchemeUrlMatchesTheBrowserServerProjectLink()
    {
        Assert.Equal(
            "stencil://open?server=http%3A%2F%2Flocalhost%3A8090&id=p_1a2b3c_x1&version=7&incognito=1",
            DesktopLinkBuilder.SchemeUrl("http://localhost:8090", "p_1a2b3c_x1", 7, incognito: true));
    }

    [Fact]
    public void SchemeUrlOmitsAnUnsetVersionAndIncognito()
    {
        Assert.Equal(
            "stencil://open?server=https%3A%2F%2Fs.example&id=p_1",
            DesktopLinkBuilder.SchemeUrl("https://s.example", "p_1"));
    }

    [Fact]
    public void SchemeUrlHonoursACustomScheme()
    {
        Assert.StartsWith(
            "stencil-dev://open?",
            DesktopLinkBuilder.SchemeUrl("https://s.example", "p_1", scheme: "stencil-dev"));
    }

    [Fact]
    public void BounceUrlWrapsTheSchemeUrlForLaunchHtml()
    {
        const string stencilUrl = "stencil://open?server=http%3A%2F%2Flocalhost%3A8090&id=p_1";
        Assert.Equal(
            "http://localhost:8080/launch.html#stencil-desktop="
            + "stencil%3A%2F%2Fopen%3Fserver%3Dhttp%253A%252F%252Flocalhost%253A8090%26id%3Dp_1",
            DesktopLinkBuilder.BounceUrl("http://localhost:8080/", stencilUrl));
    }

    [Fact]
    public void EncodingLeavesTheCharactersEncodeUriComponentLeaves()
    {
        // Uri.EscapeDataString escapes !'()* — encodeURIComponent does not, and the two
        // surfaces must emit the same link.
        Assert.Equal(
            "stencil://open?server=https%3A%2F%2Fs.example&id=!'()*",
            DesktopLinkBuilder.SchemeUrl("https://s.example", "!'()*"));
    }

    [Fact]
    public void ProjectBounceUrlKeepsABasePathAndDropsQueryFragmentAndSlashes()
    {
        Assert.Equal(
            "https://pages.example/stencil/launch.html#stencil-desktop="
            + "stencil%3A%2F%2Fopen%3Fserver%3Dhttps%253A%252F%252Fs.example%26id%3Dp_1%26version%3D3",
            DesktopLinkBuilder.TryProjectBounceUrl("https://pages.example/stencil//?x=1#frag", "https://s.example", "p_1", 3));
    }

    [Theory]
    [InlineData(null)]
    [InlineData("")]
    [InlineData("   ")]
    [InlineData("not a url")]
    [InlineData("stencil.example")]          // no scheme — never guessed for a link we hand out
    [InlineData("javascript:alert(1)")]      // launch.html would refuse it; never build it either
    [InlineData("file:///tmp/app")]
    public void ProjectBounceUrlIsNullForAnythingButAnHttpBase(string? browserBase)
    {
        Assert.Null(DesktopLinkBuilder.TryProjectBounceUrl(browserBase, "https://s.example", "p_1"));
    }
}
