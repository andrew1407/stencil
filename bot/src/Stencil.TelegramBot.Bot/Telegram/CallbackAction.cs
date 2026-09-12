using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Sessions;
using Telegram.Bot;
using Telegram.Bot.Types;
using Telegram.Bot.Types.ReplyMarkups;
using Stencil.TelegramBot.Application.Llm;

namespace Stencil.TelegramBot.Bot.Telegram;

// A tap and the equivalent slash command share one code path (CommandHandlers.DispatchAsync).
public sealed class CallbackAction
{
    // UpdateRouter routes this token AROUND the per-user gate — the turn being stopped is holding
    // it.
    public const string StopToken = "stop:prompt";

    private readonly CommandHandlers _handlers;
    private readonly ITelegramBotClient _bot;
    private readonly ISessionStore _store;
    private readonly PromptCancellations _cancellations;
    private readonly AskCardTaps _ask;

    public CallbackAction(
        CommandHandlers handlers,
        ITelegramBotClient bot,
        ISessionStore store,
        PromptCancellations? cancellations = null)
    {
        _handlers = handlers;
        _bot = bot;
        _store = store;
        _cancellations = cancellations ?? new PromptCancellations();
        _ask = new AskCardTaps(handlers, bot, store);
    }

    // The owning user is the tapper; the chat is the message the keyboard is attached to.
    public async Task HandleAsync(CallbackQuery query, CancellationToken ct)
    {
        // Answering only dismisses the spinner; a stale query (past Telegram's ~15 s window)
        // answers 400 and must not fail the tap.
        try
        {
            await _bot.AnswerCallbackQuery(query.Id, cancellationToken: ct);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
        }
        if (query.Message is null)
        {
            return;
        }
        long userId = query.From.Id;
        long chatId = query.Message.Chat.Id;
        string data = query.Data ?? "";
        if (data == "connect")
        {
            await _bot.SendMessage(chatId, Replies.ConnectUsage(), cancellationToken: ct);
            return;
        }
        if (data == "drawhelp")
        {
            await _bot.SendMessage(chatId, Replies.DrawHelp(), cancellationToken: ct);
            return;
        }
        if (data == "sources")
        {
            await _bot.SendMessage(chatId, Replies.SourcesHelp(), cancellationToken: ct);
            return;
        }
        if (data == "crophelp")
        {
            await _bot.SendMessage(chatId, Replies.CropUsage(), cancellationToken: ct);
            return;
        }
        if (data == "tinthelp")
        {
            await _bot.SendMessage(chatId, "Tint with a custom colour: /filter <colour>, e.g. /filter #ff5623 or /filter teal. (B&W, Sepia, Invert and Contour have their own buttons; None clears it.)", cancellationToken: ct);
            return;
        }
        // The next plain message is consumed as the name (see UpdateRouter).
        if (data == "name:menu")
        {
            UserSession session = await _store.GetAsync(userId, ct);
            if (!session.HasImage)
            {
                await _bot.SendMessage(
                    chatId,
                    Replies.Tag(Replies.Tone.Error, "No working image to name — upload a photo or use /blank first."),
                    cancellationToken: ct);
                return;
            }
            await _store.SaveAsync(session with { PendingInput = PendingInputs.ProjectName }, ct);
            string current = session.ActiveProjectName ?? session.ImageLabel ?? "this image";
            await _bot.SendMessage(chatId, $"Send the new name for '{current}'.", cancellationToken: ct);
            return;
        }
        // The next plain message is the description (held locally for an unsaved image, uploaded on
        // /create).
        if (data == "desc:menu")
        {
            UserSession session = await _store.GetAsync(userId, ct);
            if (!session.HasImage)
            {
                await _bot.SendMessage(
                    chatId,
                    Replies.Tag(Replies.Tone.Error, "No working image to describe — upload a photo or use /blank first."),
                    cancellationToken: ct);
                return;
            }
            await _store.SaveAsync(session with { PendingInput = PendingInputs.ProjectDescription }, ct);
            // Echoing the current description doubles as "view it" — the only place it is visible
            // besides /status.
            string currentDesc = string.IsNullOrEmpty(session.ActiveProjectDescription)
                ? "No description set yet."
                : $"Current description:\n{session.ActiveProjectDescription}";
            await _bot.SendMessage(chatId, $"{currentDesc}\n\nSend a new description (or \"-\" to clear it).", cancellationToken: ct);
            return;
        }
        // Submenu navigation swaps the keyboard in place; the main menu re-reads the session for
        // the project row.
        if (data.StartsWith("m:", StringComparison.Ordinal))
        {
            if (data == "m:download")
            {
                UserSession dl = await _store.GetAsync(userId, ct);
                await _bot.EditMessageReplyMarkup(chatId, query.Message.MessageId,
                    Keyboards.DownloadSubmenu(!dl.Edits.IsEmpty), cancellationToken: ct);
                return;
            }
            InlineKeyboardMarkup markup = data switch
            {
                "m:edit" => Keyboards.EditSubmenu(),
                "m:filter" => Keyboards.FilterSubmenu(),
                "m:draw" => Keyboards.DrawSubmenu(),
                _ => Keyboards.EditMenu(await hasActiveProjectAsync(userId, ct)),
            };
            await _bot.EditMessageReplyMarkup(chatId, query.Message.MessageId, markup, cancellationToken: ct);
            return;
        }
        // The payload is a profile NAME, looked up rather than trusted — a stale card after a
        // config change says so.
        if (data.StartsWith("api:", StringComparison.Ordinal))
        {
            await _handlers.SelectChatApiOrExplainAsync(userId, chatId, data["api:".Length..], ct);
            return;
        }
        // Arrives WHILE the turn runs, so it stays a flag flip and nothing more.
        if (data == StopToken)
        {
            await _bot.SendMessage(
                chatId,
                _cancellations.Cancel(userId)
                    ? Replies.PromptStopping()
                    : Replies.Tag(Replies.Tone.Notice, "Nothing is running — that turn already finished."),
                cancellationToken: ct);
            return;
        }
        // The button outlives its message: a prompt since answered (or lost to a restart) says so.
        if (data == "retry:prompt")
        {
            UserSession session = await _store.GetAsync(userId, ct);
            if (session.LastRetryablePrompt is not { Length: > 0 } pending)
            {
                await _bot.SendMessage(
                    chatId,
                    "That turn is no longer pending — send the prompt again.",
                    cancellationToken: ct);
                return;
            }
            await _handlers.DispatchAsync(userId, chatId, CommandParser.Prompt(pending), ct);
            return;
        }
        // §11: a tap composes the answer, it never applies anything (labels:
        // UserSession.AskOptions).
        if (data.StartsWith("ask:", StringComparison.Ordinal))
        {
            await _ask.HandleAsync(userId, chatId, query, data["ask:".Length..], ct);
            return;
        }
        // The destructive del:confirm falls through to the /delete command below.
        if (data == "del:cancel")
        {
            await _bot.EditMessageText(chatId, query.Message.MessageId, "Removal cancelled.", cancellationToken: ct);
            return;
        }
        // §10: a declined confirm is a note, never a failed plan; Yes rides the same /chat clear
        // path.
        if (data == "chatclear:cancel")
        {
            await _bot.EditMessageText(chatId, query.Message.MessageId, Replies.ClearChatCanceled(), cancellationToken: ct);
            return;
        }
        await _handlers.DispatchAsync(userId, chatId, CallbackTokens.Map(data), ct);
    }

    private async Task<bool> hasActiveProjectAsync(long userId, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(userId, ct);
        return session.ActiveProjectId is not null;
    }
}
