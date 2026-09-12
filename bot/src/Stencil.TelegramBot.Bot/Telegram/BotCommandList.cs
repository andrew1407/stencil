using TgCommand = Telegram.Bot.Types.BotCommand;

namespace Stencil.TelegramBot.Bot.Telegram;

// Registered with Telegram on startup so "/" autocomplete matches botCommands.json (a command is on
// the menu exactly when it has a menu description). Telegram wants lowercase, hyphen-free names;
// hyphenated spellings are aliases.
public static class BotCommandList
{
    public static IReadOnlyList<TgCommand> All() =>
        BotCommands.All.Where(c => c.Menu is not null).Select(c => new TgCommand(c.Verb, c.Menu!)).ToList();
}
