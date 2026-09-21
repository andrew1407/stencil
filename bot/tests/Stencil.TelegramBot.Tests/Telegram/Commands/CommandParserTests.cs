using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Bot.Telegram.Commands;

namespace Stencil.TelegramBot.Tests.Telegram.Commands;

/// <summary>The pure slash-command tokeniser: strips the leading <c>/name</c> (and a trailing <c>@botname</c>), lowercases the verb, and preserves the argument text + tokens.</summary>
public sealed class CommandParserTests
{
    [Fact]
    public void Should_Parse_Verb_Argument_Text_And_Tokens()
    {
        BotCommand command = CommandParser.Parse("/crop x1=10% x2=90%");
        Assert.Equal("crop", command.Verb);
        Assert.Equal("x1=10% x2=90%", command.ArgumentText);
        Assert.Equal(2, command.Args.Count);
        Assert.Equal("x1=10%", command.Args[0]);
        Assert.Equal("x2=90%", command.Args[1]);
    }

    [Fact]
    public void Should_Strip_The_Bot_Mention_And_Lowercase_The_Verb()
    {
        BotCommand command = CommandParser.Parse("/Connect@MyBot http://h tok");
        Assert.Equal("connect", command.Verb);
        Assert.Equal("http://h tok", command.ArgumentText);
        Assert.Equal(new[] { "http://h", "tok" }, command.Args);
    }

    [Fact]
    public void Should_Yield_Empty_Arguments_For_A_Verb_Only_Command()
    {
        BotCommand command = CommandParser.Parse("/HELP");
        Assert.Equal("help", command.Verb);
        Assert.Equal("", command.ArgumentText);
        Assert.Empty(command.Args);
    }

    [Fact]
    public void Should_Normalize_The_P_Shortcut_To_Prompt()
    {
        BotCommand command = CommandParser.Parse("/p make it sepia");
        Assert.Equal("prompt", command.Verb);
        Assert.Equal("make it sepia", command.ArgumentText);
    }

    [Fact]
    public void Should_Yield_An_Empty_Verb_For_Blank_Or_Non_Slash_Text()
    {
        Assert.Equal("", CommandParser.Parse("").Verb);
        Assert.Equal("", CommandParser.Parse("   ").Verb);
        Assert.Equal("", CommandParser.Parse(null).Verb);
        Assert.Equal("", CommandParser.Parse("just text").Verb);
    }
}
