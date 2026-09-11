using TgCommand = Telegram.Bot.Types.BotCommand;

namespace Stencil.TelegramBot.Bot.Telegram;

/// <summary>
/// The bot's slash-command menu, registered with Telegram on startup (<c>SetMyCommands</c>) so
/// the "/" autocomplete list always matches the code — no manual BotFather upkeep. The names and
/// descriptions come from the <c>botCommands.json</c> asset (<see cref="BotCommands"/>), which
/// also feeds the dispatch table and /help; a command is on the menu exactly when it carries a
/// <c>menu</c> description there. Names are lowercase and hyphen-free per Telegram's rules
/// (e.g. <c>projectcolor</c>, not <c>project-color</c>) — the hyphenated spellings are aliases.
/// </summary>
public static class BotCommandList
{
    public static IReadOnlyList<TgCommand> All() =>
        BotCommands.All.Where(c => c.Menu is not null).Select(c => new TgCommand(c.Verb, c.Menu!)).ToList();
}
