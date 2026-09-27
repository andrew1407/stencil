using Microsoft.Extensions.Logging;
using System.Collections.Concurrent;
using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Domain.Sessions;
using Telegram.Bot;
using Telegram.Bot.Types;
using Telegram.Bot.Types.Enums;
using Stencil.TelegramBot.Bot.Telegram.Messaging;

namespace Stencil.TelegramBot.Bot.Telegram.Commands;

public sealed partial class CommandHandlers
{
    // One EXTRA image (a lone variant / frame pick); a mutating plan's main result goes through
    // RenderAndSendAsync.
    private async Task sendPromptRenderAsync(long chatId, PromptRender render, UserSession session, CancellationToken ct)
    {
        await _bot.SendChatAction(chatId, ChatAction.UploadPhoto, cancellationToken: ct);
        await sendResultPhotoAsync(chatId, render.Result.Path, promptCaption(render),
            Keyboards.EditMenu(session.ActiveProjectId is not null), ct);
    }

    private static string promptCaption(PromptRender render) =>
        $"{render.Label} — {render.Result.Size}";

    // A collapsed album shows only the first caption, so it leads with the batch size.
    private static string albumLeadCaption(IReadOnlyList<PromptRender> renders) =>
        $"{renders.Count} results\n{promptCaption(renders[0])}";

    private readonly ConcurrentDictionary<long, List<PromptRender>> _renderCaptures = new();

    // Until disposed, this user's RenderAndSendAsync results are buffered instead of sent.
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
            await sendResultPhotoAsync(chatId, renders[0].Result.Path, promptCaption(renders[0]), keyboard: null, ct);
            return;
        }
        await sendPromptAlbumAsync(chatId, renders, ct);
    }

    // Falls back to sequential photos when the album send fails (e.g. an API limit).
    private async Task sendPromptAlbumAsync(long chatId, IReadOnlyList<PromptRender> renders, CancellationToken ct)
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
                    Caption = i == 0 ? albumLeadCaption(renders) : promptCaption(renders[i]),
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
            await sendResultPhotoAsync(chatId, render.Result.Path, promptCaption(render), keyboard: null, ct);
        }
    }
}
