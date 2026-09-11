using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Domain.Abstractions;
using Telegram.Bot;
using Telegram.Bot.Types;

namespace Stencil.TelegramBot.Bot.Telegram;

/// <summary>
/// Telegram media groups: buffer the members through <see cref="AlbumCollector"/> and, once the
/// group settles, run it as one batch under the user's gate and the shared error guard.
/// </summary>
public sealed class AlbumRouter
{
    private readonly MediaIntake _media;
    private readonly CommandHandlers _handlers;
    private readonly ISessionStore _store;
    private readonly ITelegramBotClient _bot;
    private readonly UserGate _gate;
    private readonly ErrorGuard _guard;
    private readonly AlbumCollector _albums;

    public AlbumRouter(
        MediaIntake media, CommandHandlers handlers, ISessionStore store, ITelegramBotClient bot,
        UserGate gate, ErrorGuard guard, AlbumCollector albums)
    {
        _media = media;
        _handlers = handlers;
        _store = store;
        _bot = bot;
        _gate = gate;
        _guard = guard;
        _albums = albums;
    }

    public void Buffer(long userId, long chatId, string groupId, Message message, PhotoSize[] album, CancellationToken ct) =>
        _albums.Add(userId, groupId,
            new AlbumPhoto(message.Id, album[^1].FileId, message.Caption),
            photos => FlushAsync(userId, chatId, photos, ct), ct);

    /// <summary>A settled album's background flush: same error guard + user gate as a message.</summary>
    private Task FlushAsync(long userId, long chatId, IReadOnlyList<AlbumPhoto> photos, CancellationToken ct) =>
        _guard.RunAsync(chatId, async () =>
        {
            using IDisposable gate = await _gate.AcquireAsync(userId, ct);
            await ProcessAsync(userId, chatId, photos, ct);
        }, ct);

    /// <summary>
    /// One settled album. With a caption (on whichever member carries it): adopt + run it once per
    /// photo in album order, buffering each render so the batch replies as ONE media group; the
    /// last photo's result stays as the working image. With no caption, only the last photo is
    /// adopted, with a single note — captionless members are never individually echoed.
    /// </summary>
    private async Task ProcessAsync(long userId, long chatId, IReadOnlyList<AlbumPhoto> photos, CancellationToken ct)
    {
        List<AlbumPhoto> ordered = photos.OrderBy(p => p.MessageId).ToList();
        await MessageRouter.ClearPendingInputAsync(_store, userId, ct);
        string? caption = ordered.FirstOrDefault(p => !string.IsNullOrWhiteSpace(p.Caption))?.Caption;
        if (caption is null)
        {
            await _bot.SendMessage(
                chatId,
                $"Got an album of {ordered.Count} photos — only one can be the working image, so I took the last. Caption an album to edit every photo.",
                cancellationToken: ct);
            await _media.AdoptImageAsync(userId, chatId, ordered[^1].FileId, ".jpg", "photo", caption: null, ct);
            return;
        }
        List<PromptRender> results = new();
        for (int i = 0; i < ordered.Count; i++)
        {
            string label = $"photo {i + 1}/{ordered.Count}";
            await _media.WithDownloadedAsync(ordered[i].FileId, ".jpg", async path =>
            {
                await _media.SetWorkingImageAsync(userId, path, label, ct);
                List<PromptRender> captured = new();
                using (_handlers.BeginRenderCapture(userId, captured))
                {
                    await _media.ApplyCaptionOrRenderAsync(userId, chatId, caption, ct);
                }
                // A caption run can render more than once; the last render is this photo's result.
                if (captured.Count > 0)
                {
                    results.Add(captured[^1]);
                }
            }, ct);
        }
        await _handlers.SendRenderAlbumAsync(chatId, results, ct);
    }
}
