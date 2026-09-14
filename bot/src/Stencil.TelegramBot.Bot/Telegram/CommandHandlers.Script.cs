using Microsoft.Extensions.Logging;
using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Sessions;
using Telegram.Bot;
using Telegram.Bot.Types.Enums;

namespace Stencil.TelegramBot.Bot.Telegram;

public sealed partial class CommandHandlers
{
    private const string _scriptUsage =
        "Usage: /script <stencil script>, e.g. /script @crop 10% ; @filter bw ; @line (0,0) (100%,100%)\n"
        + "Newlines work too, and `@source <link>:` starts a block that loads its own image. "
        + "Send a .stc file to run it as a script; the ops apply to the working image exactly like "
        + "the slash commands, so /undo walks them back.";

    private const string _scriptNeedsImage =
        "There is no working image — send a photo first, or give the script an `@source <link>:` block.";

    private Task scriptAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct) =>
        RunScriptAsync(userId, chatId, cmd.ArgumentText, ct);

    // Shared with the .stc document upload, which is the same command in file form.
    public async Task RunScriptAsync(long userId, long chatId, string text, CancellationToken ct)
    {
        if (text.Trim().Length == 0)
        {
            await _bot.SendMessage(chatId, _scriptUsage, cancellationToken: ct);
            return;
        }
        UserSession session = await _store.GetAsync(userId, ct);
        if (!session.HasImage && !hasSourceBlock(text))
        {
            await _bot.SendMessage(chatId, _scriptNeedsImage, cancellationToken: ct);
            return;
        }
        ProgressNotice working = await ProgressNotice.StartAsync(
            _bot, chatId, Replies.ScriptWorking(), ChatAction.Typing, ct);
        ScriptOutcome outcome;
        try
        {
            outcome = await _script.RunAsync(userId, text, ct);
        }
        catch (StencilCliException ex)
        {
            if (ex.OperatorDetail is string detail)
            {
                _logger.LogError("Script run failed for user {UserId}: {Detail}", userId, detail);
            }
            await _bot.SendMessage(chatId, Replies.Tag(Replies.Tone.ERROR, ex.Message), cancellationToken: ct);
            return;
        }
        finally
        {
            await working.StopAsync();
        }
        await sendScriptOutcomeAsync(userId, chatId, outcome, ct);
    }

    private async Task sendScriptOutcomeAsync(long userId, long chatId, ScriptOutcome outcome, CancellationToken ct)
    {
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
        }
        await SendRenderAlbumAsync(chatId, outcome.Renders, ct);
    }

    // A block header can only start a line, so a bare mention inside a comment or a string never
    // counts as "this script brings its own image".
    private static bool hasSourceBlock(string text)
    {
        foreach (string line in text.Split('\n'))
        {
            string trimmed = line.TrimStart();
            if (trimmed.StartsWith("@source", StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }
        }
        return false;
    }
}
