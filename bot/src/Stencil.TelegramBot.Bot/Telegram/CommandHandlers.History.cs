using Stencil.TelegramBot.Domain.Sessions;
using Telegram.Bot;

namespace Stencil.TelegramBot.Bot.Telegram;

// CommandHandlers — walking the edit history and dropping out of it: /undo, /redo, /undoline,
// /clear, /reset, /drop and the plain re-render. Class doc lives in CommandHandlers.cs.
public sealed partial class CommandHandlers
{
    private async Task UndoAsync(long userId, long chatId, CancellationToken ct)
    {
        UserSession before = await _store.GetAsync(userId, ct);
        if (!before.HasImage)
        {
            await _bot.SendMessage(chatId, "No working image — upload a photo or use /blank first.", cancellationToken: ct);
            return;
        }
        if (before.EditHistory.Count == 0)
        {
            await _bot.SendMessage(chatId, "Nothing to undo.", replyMarkup: Keyboards.EditMenu(before.ActiveProjectId is not null), cancellationToken: ct);
            return;
        }
        await _editing.UndoAsync(userId, ct);
        await RenderAndSendAsync(userId, chatId, ct);
    }

    private async Task RedoAsync(long userId, long chatId, CancellationToken ct)
    {
        UserSession before = await _store.GetAsync(userId, ct);
        if (!before.HasImage)
        {
            await _bot.SendMessage(chatId, "No working image — upload a photo or use /blank first.", cancellationToken: ct);
            return;
        }
        if (before.EditRedo.Count == 0)
        {
            await _bot.SendMessage(chatId, "Nothing to redo.", replyMarkup: Keyboards.EditMenu(before.ActiveProjectId is not null), cancellationToken: ct);
            return;
        }
        await _editing.RedoAsync(userId, ct);
        await RenderAndSendAsync(userId, chatId, ct);
    }

    private async Task UndoLineAsync(long userId, long chatId, CancellationToken ct)
    {
        UserSession session = await _editing.RemoveLastLineAsync(userId, ct);
        if (!session.HasImage)
        {
            await _bot.SendMessage(chatId, "No working image — upload a photo or use /blank first.", cancellationToken: ct);
            return;
        }
        await RenderAndSendAsync(userId, chatId, ct);
    }

    private async Task ClearLinesAsync(long userId, long chatId, CancellationToken ct)
    {
        UserSession session = await _editing.ClearLinesAsync(userId, ct);
        if (!session.HasImage)
        {
            await _bot.SendMessage(chatId, "No working image — upload a photo or use /blank first.", cancellationToken: ct);
            return;
        }
        await RenderAndSendAsync(userId, chatId, ct);
    }

    private async Task ResetAsync(long userId, long chatId, CancellationToken ct)
    {
        await _editing.ResetEditsAsync(userId, ct);
        await RenderAndSendAsync(userId, chatId, ct);
    }

    /// <summary>
    /// Drop the working image and active project entirely — a full "start over", so the
    /// assistant's conversation (which talks about that image) is forgotten along with it.
    /// </summary>
    private async Task DropAsync(long userId, long chatId, CancellationToken ct)
    {
        await _editing.DropImageAsync(userId, ct);
        _prompts.ClearHistory(userId);
        await _bot.SendMessage(
            chatId,
            "Dropped the working image. Send a photo or use /blank to start again.",
            replyMarkup: Keyboards.MainMenu(),
            cancellationToken: ct);
    }

    /// <summary>Re-render and send the current result (non-mutating — never triggers auto-sync).</summary>
    private Task ImageAsync(long userId, long chatId, CancellationToken ct) =>
        RenderAndSendAsync(userId, chatId, ct, mutating: false);
}
