using Stencil.TelegramBot.Bot.Telegram;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// <see cref="Replies.ColorDot"/> — the hex → nearest coloured-circle emoji mapping used to
/// surface a project's accent colour in chat (Telegram can't tint text).
/// </summary>
public sealed class ColorDotTests
{
    [Theory]
    [InlineData("#ff0000", "🔴")]
    [InlineData("#22cc44", "🟢")]
    [InlineData("#2277dd", "🔵")]
    [InlineData("#7c3aed", "🟣")]
    [InlineData("#111111", "⚫")]
    [InlineData("#f0f0f0", "⚪")]
    [InlineData("#f00", "🔴")]      // 3-digit hex expands
    public void MapsHexToNearestDot(string hex, string expected) =>
        Assert.Equal(expected, Replies.ColorDot(hex));

    [Fact]
    public void UnsetColourYieldsNothing()
    {
        Assert.Equal("", Replies.ColorDot(null));
        Assert.Equal("", Replies.ColorDot(""));
    }

    [Fact]
    public void NamedColourFallsBackToAPalette()
    {
        // A CSS name we don't resolve still signals "has a colour".
        Assert.Equal("🎨", Replies.ColorDot("teal"));
    }
}
