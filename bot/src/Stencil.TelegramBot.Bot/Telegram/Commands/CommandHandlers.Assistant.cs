using Microsoft.Extensions.Logging;
using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Sessions;
using Telegram.Bot;
using Telegram.Bot.Types;
using Telegram.Bot.Types.Enums;
using Stencil.TelegramBot.Bot.Telegram.Access;
using Stencil.TelegramBot.Bot.Telegram.Messaging;
using Stencil.TelegramBot.Domain.Llm.Wire;

namespace Stencil.TelegramBot.Bot.Telegram.Commands;

public sealed partial class CommandHandlers
{
    private const string _promptUsage =
        "Usage: /prompt <request>, e.g. /prompt make it black & white and crop 10% off each side\n"
        + "The AI assistant plans the edit (crop/rotate/filter/draw/…) and the bot renders it with "
        + "the same engine as every other command. Ask for alternatives to get several images. "
        + "/p is a shortcut; a photo captioned /prompt … works too.";

    // Re-reads the session first: the turn may have written it (chat history, a partly-applied
    // plan) before it ended.
    private async Task rememberForRetryAsync(long userId, string text, CancellationToken ct)
    {
        UserSession pending = await _store.GetAsync(userId, ct);
        await _store.SaveAsync(pending with { LastRetryablePrompt = text }, ct);
    }

    private async Task promptAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        string text = cmd.ArgumentText.Trim();
        if (text.Length == 0)
        {
            await _bot.SendMessage(chatId, _promptUsage, cancellationToken: ct);
            return;
        }
        UserSession session = await _store.GetAsync(userId, ct);
        // Null when there is no image or its format isn't in the accepted set — the turn is then
        // text-only.
        LlmImage? image = await _attachments.LoadAsync(session.OriginalImagePath, ct);
        // Telegram's chat action fades after ~5 s; the turn runs under its own token so the Stop
        // button can end it.
        using PromptCancellations.Registration turn = _cancellations.Begin(userId, ct);
        ProgressNotice working = await ProgressNotice.StartAsync(
            _bot, chatId, Replies.PromptWorking(), ChatAction.Typing, ct, Keyboards.StopPrompt());
        PromptOutcome outcome;
        try
        {
            outcome = await _prompts.PromptAsync(userId, text, image, turn.Token);
        }
        catch (OperationCanceledException) when (turn.Token.IsCancellationRequested && !ct.IsCancellationRequested)
        {
            // Stopped by the user: plain info, and it keeps the same Retry button a failure gets.
            await working.StopAsync();
            await rememberForRetryAsync(userId, text, ct);
            await _bot.SendMessage(
                chatId, Replies.PromptStopped(), replyMarkup: Keyboards.RetryPrompt(), cancellationToken: ct);
            return;
        }
        catch (LlmException ex)
        {
            // Never parsed as plans; deployment detail (endpoints, env vars) rides the log instead
            // of the reply.
            if (ex.OperatorDetail is string detail)
            {
                _logger.LogError("Assistant unavailable for user {UserId}: {Detail}", userId, detail);
            }
            await working.StopAsync(); // the notice goes before the failure it was covering
            // A refusal is the model's answer, not a failed call — re-sending it verbatim would
            // just repeat it.
            bool retryable = ex.Failure != LlmFailure.REFUSAL;
            if (retryable)
            {
                await rememberForRetryAsync(userId, text, ct);
            }
            await _bot.SendMessage(
                chatId,
                Replies.Tag(Replies.Tone.ERROR, ex.Message),
                replyMarkup: retryable ? Keyboards.RetryPrompt() : null,
                cancellationToken: ct);
            return;
        }
        finally
        {
            await working.StopAsync();
        }
        // The turn landed, so an older Retry button has nothing left to re-run.
        if (session.LastRetryablePrompt is not null)
        {
            UserSession answered = await _store.GetAsync(userId, ct);
            await _store.SaveAsync(answered with { LastRetryablePrompt = null }, ct);
        }
        string reply = outcome.Reply;
        if (outcome.Warnings.Count > 0)
        {
            reply += "\n\n" + string.Join("\n",
                outcome.Warnings.Select(w => Replies.Tag(Replies.Tone.WARNING, w)));
        }
        await _bot.SendMessage(chatId, reply, cancellationToken: ct);
        // The SAME render-and-send path every slash command uses, so a synced project auto-uploads.
        if (outcome.Mutated)
        {
            await RenderAndSendAsync(userId, chatId, ct);
            // The executed plan updated the stored session; fetched once here rather than once per
            // helper.
            session = await _store.GetAsync(userId, ct);
        }
        // Extra images (variant takes / extra frame picks) follow, as one album when several.
        if (outcome.Renders.Count == 1)
        {
            await sendPromptRenderAsync(chatId, outcome.Renders[0], session, ct);
        }
        else if (outcome.Renders.Count > 1)
        {
            await sendPromptAlbumAsync(chatId, outcome.Renders, ct);
        }
        // §10 export: one document per action, into the user's own chat.
        foreach (PromptExport export in outcome.Exports)
        {
            using MemoryStream stream = new(export.Bytes);
            await _bot.SendDocument(chatId, InputFile.FromStream(stream, export.FileName),
                caption: export.Caption, cancellationToken: ct);
        }
        // §11: the card goes out after the edits as its own message; a tap composes the user's next
        // turn.
        if (outcome.Ask is AskCard ask)
        {
            await sendAskCardAsync(chatId, ask, session, ct);
        }
        // §12.3: best-effort and last, so a failure can never swallow the reply or the results
        // above.
        await persistChatAsync(userId, chatId, session, ct);
        // §10 clearChat, deferred to the END of the turn: nothing is cleared here, the Yes button
        // rides /chat clear.
        if (outcome.ClearChatRequested)
        {
            await _bot.SendMessage(
                chatId,
                Replies.ClearChatConfirm(),
                replyMarkup: Keyboards.ClearChatConfirmMenu(),
                cancellationToken: ct);
        }
    }

    // §11.4: labels only — nothing here fetches on the model's behalf — stored on the session.
    private async Task sendAskCardAsync(long chatId, AskCard ask, UserSession session, CancellationToken ct)
    {
        IReadOnlyList<string> labels = ask.Options.Select(static o => o.Label).ToList();
        await _store.SaveAsync(session with { AskOptions = labels, AskMulti = ask.Multi, AskPicked = [] }, ct);

        string text = ask.Question;
        if (ask.AllowCustom)
        {
            text += $"\n\n({ask.CustomLabel} — or just type your answer)";
        }
        await _bot.SendMessage(
            chatId,
            text,
            replyMarkup: Keyboards.AskCardKeyboard(labels, ask.Multi, [], ask.AllowCustom),
            cancellationToken: ct);
    }
}
