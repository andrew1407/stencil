using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Sessions;
using Telegram.Bot;
using Telegram.Bot.Types;

namespace Stencil.TelegramBot.Bot.Telegram;

// A tap only COMPOSES the answer (§11.3); the composed labels go back through the ordinary prompt
// path.
public sealed class AskCardTaps
{
    private readonly CommandHandlers _handlers;
    private readonly ITelegramBotClient _bot;
    private readonly ISessionStore _store;

    public AskCardTaps(CommandHandlers handlers, ITelegramBotClient bot, ISessionStore store)
    {
        _handlers = handlers;
        _bot = bot;
        _store = store;
    }

    // ask:<n> picks (single-select submits at once, multi toggles); ask:send submits; a stale card
    // says so.
    public async Task HandleAsync(long userId, long chatId, CallbackQuery query, string token, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(userId, ct);
        if (session.AskOptions.Count == 0)
        {
            await _bot.EditMessageText(chatId, query.Message!.MessageId,
                "That question is no longer open — just type what you want.", cancellationToken: ct);
            return;
        }
        if (token == "custom")
        {
            await _bot.SendMessage(chatId, "Go ahead — type your answer.", cancellationToken: ct);
            return;
        }

        List<int> picked = session.AskPicked.ToList();
        if (token != "send")
        {
            if (!int.TryParse(token, out int index) || index < 0 || index >= session.AskOptions.Count)
            {
                return;   // a button from a card that has since been replaced
            }
            if (!session.AskMulti)
            {
                await submitAsync(userId, chatId, session, [index], ct);
                return;
            }
            if (!picked.Remove(index))
            {
                picked.Add(index);
            }
            await _store.SaveAsync(session with { AskPicked = picked }, ct);
            await _bot.EditMessageReplyMarkup(chatId, query.Message!.MessageId,
                Keyboards.AskCardKeyboard(session.AskOptions, true, picked, allowCustom: false), cancellationToken: ct);
            return;
        }
        if (picked.Count == 0)
        {
            await _bot.SendMessage(
                chatId, Replies.Tag(Replies.Tone.ERROR, "Pick at least one option first."), cancellationToken: ct);
            return;
        }
        await submitAsync(userId, chatId, session, picked, ct);
    }

    // The same prompt path typing them would take; the card is retired so it cannot be answered
    // twice.
    private async Task submitAsync(long userId, long chatId, UserSession session, IReadOnlyList<int> picked, CancellationToken ct)
    {
        string answer = OpPlanParser.AskAnswerText(picked.Select(i => session.AskOptions[i]));
        await _store.SaveAsync(session with { AskOptions = [], AskMulti = false, AskPicked = [] }, ct);
        if (answer.Length == 0)
        {
            return;
        }
        await _handlers.DispatchAsync(userId, chatId, CommandParser.Prompt(answer), ct);
    }
}
