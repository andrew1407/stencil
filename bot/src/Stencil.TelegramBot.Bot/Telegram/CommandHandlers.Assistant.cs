using Microsoft.Extensions.Logging;
using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Sessions;
using Telegram.Bot;
using Telegram.Bot.Types;
using Telegram.Bot.Types.Enums;

namespace Stencil.TelegramBot.Bot.Telegram;

// CommandHandlers — one assistant turn: /prompt, its retry parking and the §11 ask card.
// Class doc lives in CommandHandlers.cs.
public sealed partial class CommandHandlers
{
    private const string PromptUsage =
        "Usage: /prompt <request>, e.g. /prompt make it black & white and crop 10% off each side\n"
        + "The AI assistant plans the edit (crop/rotate/filter/draw/…) and the bot renders it with "
        + "the same engine as every other command. Ask for alternatives to get several images. "
        + "/p is a shortcut; a photo captioned /prompt … works too.";

    /// <summary>
    /// Park a prompt that never delivered (it failed, or was stopped) so the 🔄 Retry button on
    /// that message can re-run it. Re-reads the session first: the turn may have written it (chat
    /// history, a partly-applied plan) before it ended.
    /// </summary>
    private async Task RememberForRetryAsync(long userId, string text, CancellationToken ct)
    {
        UserSession pending = await _store.GetAsync(userId, ct);
        await _store.SaveAsync(pending with { LastRetryablePrompt = text }, ct);
    }

    private async Task PromptAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        string text = cmd.ArgumentText.Trim();
        if (text.Length == 0)
        {
            await _bot.SendMessage(chatId, PromptUsage, cancellationToken: ct);
            return;
        }
        UserSession session = await _store.GetAsync(userId, ct);
        // The working image rides along as this turn's vision attachment (downscaled to the
        // contract's ≤ 1568 px long edge when oversized); null when there is no image or its
        // format isn't in the accepted set — the turn is then text-only.
        LlmImage? image = await _attachments.LoadAsync(session.OriginalImagePath, ct);
        // The model call can take minutes, so a spinning notice goes out first (Telegram's chat
        // action alone fades after ~5 s and is easy to miss). The turn runs under its own token
        // so the notice's Stop button can end it (see PromptCancellations).
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
            // The user stopped it — plain info, not an error. The request never got an answer,
            // so it keeps the same Retry button a failure gets.
            await working.StopAsync();
            await RememberForRetryAsync(userId, text, ct);
            await _bot.SendMessage(
                chatId, Replies.PromptStopped(), replyMarkup: Keyboards.RetryPrompt(), cancellationToken: ct);
            return;
        }
        catch (LlmException ex)
        {
            // Transport/config errors plus the contract's truncated/refusal stop reasons —
            // all surfaced as chat text, never parsed as plans. Deployment detail (endpoints,
            // env vars) rides the log instead of the reply.
            if (ex.OperatorDetail is string detail)
            {
                _logger.LogError("Assistant unavailable for user {UserId}: {Detail}", userId, detail);
            }
            await working.StopAsync(); // the notice goes before the failure it was covering
            // A failure the user can act on gets a Retry button, so recovering from a timed-out
            // or unreachable endpoint is one tap instead of retyping the turn. A refusal is the
            // model's answer, not a failed call — re-sending it verbatim would just repeat it.
            bool retryable = ex.Failure != LlmFailure.Refusal;
            if (retryable)
            {
                await RememberForRetryAsync(userId, text, ct);
            }
            await _bot.SendMessage(
                chatId,
                Replies.Tag(Replies.Tone.Error, ex.Message),
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
                outcome.Warnings.Select(w => Replies.Tag(Replies.Tone.Warning, w)));
        }
        await _bot.SendMessage(chatId, reply, cancellationToken: ct);
        // A mutating plan sends its main result through the SAME render-and-send path every
        // slash command uses — one caption/keyboard shape, and a synced project auto-uploads.
        if (outcome.Mutated)
        {
            await RenderAndSendAsync(userId, chatId, ct);
            // The executed plan (and a live-sync save) updated the stored session, so the tail
            // helpers below read a fresh copy — fetched once here rather than once per helper.
            session = await _store.GetAsync(userId, ct);
        }
        // Extra images (variant takes / extra frame picks) follow, as one album when several.
        if (outcome.Renders.Count == 1)
        {
            await SendPromptRenderAsync(chatId, outcome.Renders[0], session, ct);
        }
        else if (outcome.Renders.Count > 1)
        {
            await SendPromptAlbumAsync(chatId, outcome.Renders, ct);
        }
        // §10 `export`: each action produced exactly ONE document (the same bytes /json and
        // /project send) — delivered here, into the user's own chat, one send per action.
        foreach (PromptExport export in outcome.Exports)
        {
            using MemoryStream stream = new(export.Bytes);
            await _bot.SendDocument(chatId, InputFile.FromStream(stream, export.FileName),
                caption: export.Caption, cancellationToken: ct);
        }
        // §11: the plan may also ASK. The card goes out after the edits, as its own message with
        // an inline keyboard; tapping composes the answer and sends it as the user's next turn.
        if (outcome.Ask is AskCard ask)
        {
            await SendAskCardAsync(chatId, ask, session, ct);
        }
        // Contract §12.3: with /chat save on and an active server project, mirror the (text-only,
        // displayed-reply) conversation to the project's `chat` file kind. Best-effort — last, so
        // a failure can never swallow the reply or the rendered results above.
        await PersistChatAsync(userId, chatId, session, ct);
        // §10 clearChat, deferred to the END of the turn: the confirmation goes out last, and
        // nothing is cleared here — the Yes button rides the same /chat clear path
        // (ClearChatHistoryAsync); Cancel just notes it.
        if (outcome.ClearChatRequested)
        {
            await _bot.SendMessage(
                chatId,
                Replies.ClearChatConfirm(),
                replyMarkup: Keyboards.ClearChatConfirmMenu(),
                cancellationToken: ct);
        }
    }

    /// <summary>
    /// Send an <c>ask</c> card (contract §11.4: an inline keyboard, one button per option, a
    /// Send button when it takes several). Labels only — nothing here fetches on the model's
    /// behalf — stored on the session (see <c>UserSession.AskOptions</c>).
    /// </summary>
    private async Task SendAskCardAsync(long chatId, AskCard ask, UserSession session, CancellationToken ct)
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
