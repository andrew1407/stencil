using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Sessions;
using Telegram.Bot;
using Telegram.Bot.Types;
using Telegram.Bot.Types.ReplyMarkups;
using Stencil.TelegramBot.Application.Llm;

namespace Stencil.TelegramBot.Bot.Telegram;

// Maps an inline button's short token (see Keyboards) back to a synthetic BotCommand and runs
// it through CommandHandlers.DispatchAsync, so a tap and the equivalent slash command share one
// code path. A button that cannot act without an argument replies with the command to use.
public sealed class CallbackAction
{
    // UpdateRouter matches on this to route the tap AROUND the per-user gate — the turn being
    // stopped is holding it.
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
        // Best-effort: answering only dismisses the button's spinner. A query Telegram considers
        // stale (its ~15 s window elapsed, e.g. a tap that waited behind a long turn) answers 400,
        // and that must not become "something went wrong" — the tap itself is still worth running.
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
        // Arg-requiring or help-only buttons can't act on their own — reply with guidance.
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
        // Rename can't act on a single tap (it needs the new name), so arm the pending free-text
        // prompt — the user's next plain message is consumed as the name (see UpdateRouter).
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
        // Describe: same as Rename — arm the free-text prompt; the next plain message is the
        // description (which for an unsaved image is held locally and uploaded on /create).
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
            // Echo the current description first (like Rename echoes the current name) so this
            // doubles as "view it" — the only place the description is visible besides /status.
            string currentDesc = string.IsNullOrEmpty(session.ActiveProjectDescription)
                ? "No description set yet."
                : $"Current description:\n{session.ActiveProjectDescription}";
            await _bot.SendMessage(chatId, $"{currentDesc}\n\nSend a new description (or \"-\" to clear it).", cancellationToken: ct);
            return;
        }
        // Group buttons swap the inline keyboard in place (submenu navigation), no edit performed.
        // Returning to the main edit menu re-reads the session so the project-actions row (shown
        // only for a server project) is restored after a submenu detour.
        if (data.StartsWith("m:", StringComparison.Ordinal))
        {
            // The Download submenu offers the layout JSON only when there are applied edits.
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
                _ => Keyboards.EditMenu(await HasActiveProjectAsync(userId, ct)),
            };
            await _bot.EditMessageReplyMarkup(chatId, query.Message.MessageId, markup, cancellationToken: ct);
            return;
        }
        // Chat-API picker: the payload is a profile NAME the operator configured, looked up
        // rather than trusted — an unknown one (a stale card after a config change) says so.
        if (data.StartsWith("api:", StringComparison.Ordinal))
        {
            await _handlers.SelectChatApiOrExplainAsync(userId, chatId, data["api:".Length..], ct);
            return;
        }
        // Stop the running assistant turn. Handled before anything touches the session: this tap
        // arrives WHILE the turn runs (that is the point), so it stays a flag flip and nothing more.
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
        // Retry on an assistant turn that failed or was stopped: re-run the stored prompt through
        // the SAME handler the slash command uses. The button outlives its message, so a prompt
        // that has since been answered (or lost to a restart) says so rather than re-sending a
        // mystery turn.
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
        // §11 choice card: a tap composes the answer, it never applies anything. Single-select
        // sends straight away; multi-select toggles a tick and waits for Send. The labels come
        // from the session (see UserSession.AskOptions for why).
        if (data.StartsWith("ask:", StringComparison.Ordinal))
        {
            await _ask.HandleAsync(userId, chatId, query, data["ask:".Length..], ct);
            return;
        }
        // Cancel on the delete confirmation just retires the prompt (the confirmation is its own
        // message; the destructive del:confirm falls through to the /delete command below).
        if (data == "del:cancel")
        {
            await _bot.EditMessageText(chatId, query.Message.MessageId, "Removal cancelled.", cancellationToken: ct);
            return;
        }
        // §10 clearChat: a declined confirm is a "clear canceled" note, never a failed plan;
        // the Yes button falls through to Map, riding the same /chat clear path as the 🧹 button.
        if (data == "chatclear:cancel")
        {
            await _bot.EditMessageText(chatId, query.Message.MessageId, Replies.ClearChatCanceled(), cancellationToken: ct);
            return;
        }
        await _handlers.DispatchAsync(userId, chatId, CallbackTokens.Map(data), ct);
    }

    private async Task<bool> HasActiveProjectAsync(long userId, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(userId, ct);
        return session.ActiveProjectId is not null;
    }
}
