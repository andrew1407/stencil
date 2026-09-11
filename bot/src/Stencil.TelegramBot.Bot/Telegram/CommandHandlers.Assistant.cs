using System.Collections.Concurrent;
using System.Globalization;
using System.Text;
using Microsoft.Extensions.Logging;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Links;
using Telegram.Bot;
using Telegram.Bot.Types;
using Telegram.Bot.Types.Enums;
using Telegram.Bot.Types.ReplyMarkups;

namespace Stencil.TelegramBot.Bot.Telegram;

// CommandHandlers — AI-assistant commands: /prompt, /chat, /chatapi, ask cards, chat
// persistence, and the prompt render/album senders. Class doc lives in CommandHandlers.cs.
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
    /// Push the §12.1 chat document to the active server project when chat saving is on. A
    /// failure never fails the turn: it is logged and surfaced as a short warning only once
    /// (<see cref="UserSession.ChatSaveWarned"/>, re-armed by the next successful save).
    /// </summary>
    private async Task PersistChatAsync(long userId, long chatId, UserSession session, CancellationToken ct)
    {
        if (!session.SaveChats || session.ActiveProjectId is null)
        {
            return;
        }
        if (_prompts.BuildChatDocument(userId) is not ChatDocument doc || doc.Messages.Count == 0)
        {
            return;
        }
        try
        {
            await _servers.SaveChatAsync(userId, doc.ToJson(), ct);
            if (session.ChatSaveWarned)
            {
                await SetChatSaveWarnedAsync(userId, false, ct);
            }
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            _logger.LogWarning(ex, "Chat save-back to the server failed for user {UserId}", userId);
            if (!session.ChatSaveWarned)
            {
                await SetChatSaveWarnedAsync(userId, true, ct);
                await _bot.SendMessage(chatId, Replies.ChatSaveFailed(), cancellationToken: ct);
            }
        }
    }

    /// <summary>
    /// Flip the persisted once-only chat-save warning flag. Re-fetches before saving: the ask
    /// card sent earlier in the same turn may have stored state the caller's copy predates.
    /// </summary>
    private async Task SetChatSaveWarnedAsync(long userId, bool warned, CancellationToken ct)
    {
        UserSession latest = await _store.GetAsync(userId, ct);
        await _store.SaveAsync(latest with { ChatSaveWarned = warned }, ct);
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

    /// <summary>
    /// <c>/chatapi</c> — show the chat APIs this bot offers and which one the caller is on, with
    /// a button per profile. <c>/chatapi &lt;name&gt;</c> selects one without the picker.
    /// </summary>
    /// <remarks>
    /// The list is the operator's (<c>STENCIL_LLM_PROFILES</c>); a user picks from it and never
    /// types an endpoint — see <see cref="LlmProfile"/> for why. The choice is per user and lives
    /// in their session, so it survives restarts and shows up in /status.
    /// </remarks>
    private async Task ChatApiAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        if (_options.LlmProfiles.Count == 0)
        {
            await _bot.SendMessage(chatId, Replies.ChatApiNoProfiles(), cancellationToken: ct);
            return;
        }
        UserSession session = await _store.GetAsync(userId, ct);
        string wanted = cmd.ArgumentText.Trim();
        if (wanted.Length == 0)
        {
            await _bot.SendMessage(
                chatId,
                Replies.ChatApiList(_options.LlmProfiles, _options.FindProfile(session.LlmProfile)),
                replyMarkup: Keyboards.ChatApiMenu(_options.LlmProfiles, session.LlmProfile),
                cancellationToken: ct);
            return;
        }
        if (_options.FindProfile(wanted) is not LlmProfile picked)
        {
            await _bot.SendMessage(chatId, Replies.ChatApiUnknown(wanted, _options.LlmProfiles), cancellationToken: ct);
            return;
        }
        await SelectChatApiAsync(userId, chatId, picked, ct);
    }

    /// <summary>
    /// A tap on the picker: select <paramref name="name"/>, or say it is no longer offered — a
    /// card can outlive the configuration it was drawn from.
    /// </summary>
    public async Task SelectChatApiOrExplainAsync(long userId, long chatId, string name, CancellationToken ct)
    {
        if (_options.FindProfile(name) is not LlmProfile picked)
        {
            await _bot.SendMessage(chatId, Replies.ChatApiUnknown(name, _options.LlmProfiles), cancellationToken: ct);
            return;
        }
        await SelectChatApiAsync(userId, chatId, picked, ct);
    }

    public async Task SelectChatApiAsync(long userId, long chatId, LlmProfile picked, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(userId, ct);
        await _store.SaveAsync(session with { LlmProfile = picked.Name }, ct);
        await _bot.SendMessage(
            chatId,
            Replies.ChatApiSelected(picked),
            replyMarkup: Keyboards.ChatApiMenu(_options.LlmProfiles, picked.Name),
            cancellationToken: ct);
    }

    /// <summary>
    /// <c>/chat [on|off|clear]</c> — the same three actions the 💬 / 🚪 / 🧹 buttons ride. While
    /// on, <see cref="UpdateRouter"/> hands each unclaimed plain message to
    /// <see cref="PromptAsync"/>, so chatting and <c>/prompt</c> are literally the same path.
    /// </summary>
    private async Task ChatAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        string arg = cmd.ArgumentText.Trim().ToLowerInvariant();
        if (arg is "clear" or "reset" or "forget" or "new")
        {
            // Never gated: someone who used the assistant before the allowlist tightened
            // must still be able to delete what it stored.
            await ClearChatHistoryAsync(userId, chatId, ct);
            return;
        }
        if (arg == "save" || arg.StartsWith("save ", StringComparison.Ordinal))
        {
            await ChatSaveAsync(userId, chatId, arg["save".Length..].Trim(), ct);
            return;
        }
        bool? enable = arg switch
        {
            "" or "on" or "start" => true,
            "off" or "stop" or "end" or "exit" => false,
            _ => null,
        };
        if (enable is not bool on)
        {
            await _bot.SendMessage(chatId, Replies.ChatUsage(), cancellationToken: ct);
            return;
        }
        UserSession session = await _store.GetAsync(userId, ct);
        if (session.ChatMode != on)
        {
            await _store.SaveAsync(session with { ChatMode = on }, ct);
        }
        if (on)
        {
            await _bot.SendMessage(chatId, Replies.ChatModeOn(), replyMarkup: Keyboards.ChatModeMenu(session.SaveChats), cancellationToken: ct);
        }
        else
        {
            await _bot.SendMessage(chatId, Replies.ChatModeOff(), cancellationToken: ct);
        }
    }

    /// <summary>
    /// Chat persistence toggle (contract §12.3, default OFF): <c>/chat save [on|off]</c>. The
    /// store is the active SERVER project's <c>chat</c> file kind — no local copy. Turning it
    /// off stops writing but does not delete an already-saved chat (<c>/chat clear</c> does).
    /// </summary>
    private async Task ChatSaveAsync(long userId, long chatId, string arg, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(userId, ct);
        if (arg.Length == 0)
        {
            await _bot.SendMessage(
                chatId,
                Replies.ChatSaveStatus(session.SaveChats),
                replyMarkup: session.ChatMode ? Keyboards.ChatModeMenu(session.SaveChats) : null,
                cancellationToken: ct);
            return;
        }
        bool? enable = arg switch
        {
            "on" or "true" or "1" or "yes" => true,
            "off" or "false" or "0" or "no" => false,
            _ => null,
        };
        if (enable is not bool on)
        {
            await _bot.SendMessage(chatId, Replies.ChatUsage(), cancellationToken: ct);
            return;
        }
        if (session.SaveChats != on)
        {
            await _store.SaveAsync(session with { SaveChats = on }, ct);
        }
        await _bot.SendMessage(
            chatId,
            on ? Replies.ChatSaveOn() : Replies.ChatSaveOff(),
            replyMarkup: session.ChatMode ? Keyboards.ChatModeMenu(on) : null,
            cancellationToken: ct);
    }

    /// <summary>
    /// Forget the assistant's conversation (<c>/chat clear</c>, the 🧹 button, the §10 confirm
    /// button). Chat mode, image and edits are untouched. Per §12.2 it also removes the persisted
    /// server copy when chat saving is on (best-effort — an unreachable server never blocks it).
    /// </summary>
    private async Task ClearChatHistoryAsync(long userId, long chatId, CancellationToken ct)
    {
        _prompts.ClearHistory(userId);
        UserSession session = await _store.GetAsync(userId, ct);
        if (session.SaveChats && session.ActiveProjectId is not null)
        {
            try
            {
                await _servers.DeleteChatAsync(userId, ct);
            }
            catch (Exception ex) when (ex is not OperationCanceledException)
            {
                _logger.LogWarning(ex, "Server chat delete failed for user {UserId}", userId);
            }
        }
        await _bot.SendMessage(
            chatId,
            Replies.ChatHistoryCleared(session.ChatMode),
            replyMarkup: session.ChatMode ? Keyboards.ChatModeMenu(session.SaveChats) : null,
            cancellationToken: ct);
    }

    /// <summary>
    /// Send one EXTRA prompt image (a lone variant take / extra frame pick) as a photo with the
    /// edit menu. The main result of a mutating plan goes through <see cref="RenderAndSendAsync"/>
    /// instead, like every slash command.
    /// </summary>
    private async Task SendPromptRenderAsync(long chatId, PromptRender render, UserSession session, CancellationToken ct)
    {
        await _bot.SendChatAction(chatId, ChatAction.UploadPhoto, cancellationToken: ct);
        await SendResultPhotoAsync(chatId, render.Result.Path, PromptCaption(render),
            Keyboards.EditMenu(session.ActiveProjectId is not null), ct);
    }

    private static string PromptCaption(PromptRender render) =>
        $"{render.Label} — {render.Result.Size}";

    /// <summary>
    /// The caption an album's FIRST item carries. A collapsed album shows only that one, so it
    /// leads with the batch size: without it a three-result album reads "photo 1/3", as if the
    /// other two had gone missing. Every item still keeps its own caption for the opened view.
    /// </summary>
    private static string AlbumLeadCaption(IReadOnlyList<PromptRender> renders) =>
        $"{renders.Count} results\n{PromptCaption(renders[0])}";

    /// <summary>Active album-batch captures: while set, a user's renders buffer instead of sending.</summary>
    private readonly ConcurrentDictionary<long, List<PromptRender>> _renderCaptures = new();

    /// <summary>
    /// Open an album-batch scope: until disposed, this user's <see cref="RenderAndSendAsync"/>
    /// results are buffered into <paramref name="captured"/> instead of being sent — the batch
    /// then replies as one media group via <see cref="SendRenderAlbumAsync"/>.
    /// </summary>
    public IDisposable BeginRenderCapture(long userId, List<PromptRender> captured)
    {
        _renderCaptures[userId] = captured;
        return new RenderCaptureScope(this, userId);
    }

    private sealed class RenderCaptureScope(CommandHandlers owner, long userId) : IDisposable
    {
        public void Dispose() => owner._renderCaptures.TryRemove(userId, out _);
    }

    public async Task SendRenderAlbumAsync(long chatId, IReadOnlyList<PromptRender> renders, CancellationToken ct)
    {
        if (renders.Count == 0)
        {
            return;
        }
        if (renders.Count == 1)
        {
            await _bot.SendChatAction(chatId, ChatAction.UploadPhoto, cancellationToken: ct);
            await SendResultPhotoAsync(chatId, renders[0].Result.Path, PromptCaption(renders[0]), keyboard: null, ct);
            return;
        }
        await SendPromptAlbumAsync(chatId, renders, ct);
    }

    /// <summary>
    /// Send multiple prompt results (variants / multi-frame picks) as one media album, falling
    /// back to sequential photos when the album send fails (e.g. an API limit).
    /// </summary>
    private async Task SendPromptAlbumAsync(long chatId, IReadOnlyList<PromptRender> renders, CancellationToken ct)
    {
        await _bot.SendChatAction(chatId, ChatAction.UploadPhoto, cancellationToken: ct);
        List<FileStream> streams = new();
        try
        {
            List<IAlbumInputMedia> media = new();
            for (int i = 0; i < renders.Count; i++)
            {
                FileStream stream = File.OpenRead(renders[i].Result.Path);
                streams.Add(stream);
                media.Add(new InputMediaPhoto(InputFile.FromStream(stream, $"result-{i + 1}.png"))
                {
                    Caption = i == 0 ? AlbumLeadCaption(renders) : PromptCaption(renders[i]),
                });
            }
            await _bot.SendMediaGroup(chatId, media, cancellationToken: ct);
            return;
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            _logger.LogWarning(ex, "Media-group send failed; falling back to sequential photos");
        }
        finally
        {
            foreach (FileStream stream in streams)
            {
                await stream.DisposeAsync();
            }
        }
        foreach (PromptRender render in renders)
        {
            await SendResultPhotoAsync(chatId, render.Result.Path, PromptCaption(render), keyboard: null, ct);
        }
    }

    /// <summary>
    /// Pull the fetched project's persisted chat (§12.1) and seed the assistant's history from
    /// it. Only with chat saving on; best-effort (a missing/invalid document or an unreachable
    /// file route restores nothing). Returns the number of restored messages.
    /// </summary>
    private async Task<int> TryRestoreChatAsync(long userId, UserSession session, CancellationToken ct)
    {
        if (!session.SaveChats || session.ActiveProjectId is null)
        {
            return 0;
        }
        try
        {
            string? json = await _servers.LoadChatAsync(userId, ct);
            if (json is null || ChatDocument.TryParse(json) is not ChatDocument doc || doc.Messages.Count == 0)
            {
                return 0;
            }
            return _prompts.SeedHistory(userId, doc);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            _logger.LogWarning(ex, "Chat restore from the server failed for user {UserId}", userId);
            return 0;
        }
    }
}
