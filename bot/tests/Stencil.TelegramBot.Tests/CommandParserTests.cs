using Stencil.TelegramBot.Bot.Telegram;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The pure slash-command tokeniser <see cref="CommandParser"/>: strips the leading <c>/name</c>
/// (and a trailing <c>@botname</c>), lowercases the verb, and preserves the argument text + tokens.
/// </summary>
public sealed class CommandParserTests
{
    [Fact]
    public void ParsesVerbArgumentTextAndTokens()
    {
        BotCommand command = CommandParser.Parse("/crop x1=10% x2=90%");
        Assert.Equal("crop", command.Verb);
        Assert.Equal("x1=10% x2=90%", command.ArgumentText);
        Assert.Equal(2, command.Args.Count);
        Assert.Equal("x1=10%", command.Args[0]);
        Assert.Equal("x2=90%", command.Args[1]);
    }

    [Fact]
    public void StripsBotMentionAndLowercasesVerb()
    {
        BotCommand command = CommandParser.Parse("/Connect@MyBot http://h tok");
        Assert.Equal("connect", command.Verb);
        Assert.Equal("http://h tok", command.ArgumentText);
        Assert.Equal(new[] { "http://h", "tok" }, command.Args);
    }

    [Fact]
    public void VerbOnlyHasEmptyArguments()
    {
        BotCommand command = CommandParser.Parse("/HELP");
        Assert.Equal("help", command.Verb);
        Assert.Equal("", command.ArgumentText);
        Assert.Empty(command.Args);
    }

    [Fact]
    public void BlankOrNonSlashYieldsEmptyVerb()
    {
        Assert.Equal("", CommandParser.Parse("").Verb);
        Assert.Equal("", CommandParser.Parse("   ").Verb);
        Assert.Equal("", CommandParser.Parse(null).Verb);
        Assert.Equal("", CommandParser.Parse("just text").Verb);
    }
}
