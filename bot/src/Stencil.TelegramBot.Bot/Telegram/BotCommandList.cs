using TgCommand = Telegram.Bot.Types.BotCommand;

namespace Stencil.TelegramBot.Bot.Telegram;

// Registered with Telegram on startup (SetMyCommands), so "/" autocomplete matches the code with
// no BotFather upkeep. Names and descriptions come from the botCommands.json asset, which also
// feeds the dispatch table and /help; a command is on the menu exactly when it has a `menu`
// description there. Telegram's rules make the names lowercase and hyphen-free (projectcolor,
// not project-color) — the hyphenated spellings are aliases.
public static class BotCommandList
{
    public static IReadOnlyList<TgCommand> All() =>
        BotCommands.All.Where(c => c.Menu is not null).Select(c => new TgCommand(c.Verb, c.Menu!)).ToList();
}
