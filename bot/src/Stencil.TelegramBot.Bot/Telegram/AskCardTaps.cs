using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Sessions;
using Telegram.Bot;
using Telegram.Bot.Types;

namespace Stencil.TelegramBot.Bot.Telegram;

/// <summary>
/// Taps on an <c>ask</c> card (contract §11.3). A tap only COMPOSES the answer — it never
/// applies anything; the composed labels go back through the ordinary prompt path.
/// </summary>
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

    /// <summary>
    /// Handle a tap on an <c>ask</c> card (contract §11.3). <c>ask:&lt;n&gt;</c> picks an option —
    /// on a single-select card that submits at once; on a multi-select one it toggles the tick and
    /// redraws the keyboard. <c>ask:send</c> submits the ticked labels, <c>ask:custom</c> just
    /// invites typing. A card whose session state is gone (a restart, or an older card) says so
    /// rather than sending a mystery answer.
    /// </summary>
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
                await SubmitAsync(userId, chatId, session, [index], ct);
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
                chatId, Replies.Tag(Replies.Tone.Error, "Pick at least one option first."), cancellationToken: ct);
            return;
        }
        await SubmitAsync(userId, chatId, session, picked, ct);
    }

    /// <summary>
    /// Send the chosen labels as the user's next turn — the same prompt path typing them would
    /// take — and retire the card so it cannot be answered twice.
    /// </summary>
    private async Task SubmitAsync(long userId, long chatId, UserSession session, IReadOnlyList<int> picked, CancellationToken ct)
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
