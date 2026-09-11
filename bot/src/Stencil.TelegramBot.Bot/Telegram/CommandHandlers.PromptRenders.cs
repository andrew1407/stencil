using Microsoft.Extensions.Logging;
using System.Collections.Concurrent;
using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Domain.Sessions;
using Telegram.Bot;
using Telegram.Bot.Types;
using Telegram.Bot.Types.Enums;

namespace Stencil.TelegramBot.Bot.Telegram;

// CommandHandlers — how a prompt turn's images reach the chat: one extra render, or a whole
// batch buffered into a media album. Class doc lives in CommandHandlers.cs.
public sealed partial class CommandHandlers
{
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
}
