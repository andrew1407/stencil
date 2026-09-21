using Stencil.TelegramBot.Domain.Sessions;
using Telegram.Bot;
using Stencil.TelegramBot.Bot.Telegram.Messaging;

namespace Stencil.TelegramBot.Bot.Telegram.Commands;

public sealed partial class CommandHandlers
{
    private async Task undoAsync(long userId, long chatId, CancellationToken ct)
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

    private async Task redoAsync(long userId, long chatId, CancellationToken ct)
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

    private async Task undoLineAsync(long userId, long chatId, CancellationToken ct)
    {
        UserSession session = await _editing.RemoveLastLineAsync(userId, ct);
        if (!session.HasImage)
        {
            await _bot.SendMessage(chatId, "No working image — upload a photo or use /blank first.", cancellationToken: ct);
            return;
        }
        await RenderAndSendAsync(userId, chatId, ct);
    }

    private async Task clearLinesAsync(long userId, long chatId, CancellationToken ct)
    {
        UserSession session = await _editing.ClearLinesAsync(userId, ct);
        if (!session.HasImage)
        {
            await _bot.SendMessage(chatId, "No working image — upload a photo or use /blank first.", cancellationToken: ct);
            return;
        }
        await RenderAndSendAsync(userId, chatId, ct);
    }

    private async Task resetAsync(long userId, long chatId, CancellationToken ct)
    {
        await _editing.ResetEditsAsync(userId, ct);
        await RenderAndSendAsync(userId, chatId, ct);
    }

    // A full "start over": the assistant's conversation (which talks about that image) is forgotten
    // too.
    private async Task dropAsync(long userId, long chatId, CancellationToken ct)
    {
        await _editing.DropImageAsync(userId, ct);
        _prompts.ClearHistory(userId);
        await _bot.SendMessage(
            chatId,
            "Dropped the working image. Send a photo or use /blank to start again.",
            replyMarkup: Keyboards.MainMenu(),
            cancellationToken: ct);
    }

    // Non-mutating — never triggers auto-sync.
    private Task imageAsync(long userId, long chatId, CancellationToken ct) =>
        RenderAndSendAsync(userId, chatId, ct, mutating: false);
}
