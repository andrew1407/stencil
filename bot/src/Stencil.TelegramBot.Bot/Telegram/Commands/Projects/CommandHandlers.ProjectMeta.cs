using Stencil.TelegramBot.Domain.Sessions;
using Telegram.Bot;
using Stencil.TelegramBot.Bot.Telegram.Messaging;

namespace Stencil.TelegramBot.Bot.Telegram.Commands;

public sealed partial class CommandHandlers
{
    private async Task projectColorAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        if (cmd.Args.Count == 0)
        {
            await _bot.SendMessage(chatId, Replies.ProjectColorUsage(), cancellationToken: ct);
            return;
        }
        string arg = cmd.Args[0];
        string color = arg is "clear" or "none" or "default" ? "" : arg;
        string effective = await _servers.SetProjectColorAsync(userId, color, ct);
        await _bot.SendMessage(
            chatId,
            Replies.ProjectColorSet(effective),
            cancellationToken: ct);
    }

    private async Task projectNameAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        // Project names may contain spaces.
        string name = cmd.ArgumentText.Trim();
        if (name.Length == 0)
        {
            await _bot.SendMessage(chatId, Replies.ProjectNameUsage(), cancellationToken: ct);
            return;
        }
        UserSession session = await _store.GetAsync(userId, ct);
        // A saved server project renames on the server (version-guarded, broadcast to peers).
        if (session.ActiveProjectId is not null)
        {
            string effective = await _servers.SetProjectNameAsync(userId, name, ct);
            await _bot.SendMessage(chatId, Replies.ProjectRenamed(effective), cancellationToken: ct);
            return;
        }
        // No server project yet: relabel the local working image (the /create default name).
        if (!session.HasImage)
        {
            await _bot.SendMessage(chatId, Replies.NoImageToName(), cancellationToken: ct);
            return;
        }
        await _store.SaveAsync(session with { ImageLabel = name }, ct);
        await _bot.SendMessage(chatId, Replies.ImageRenamed(name), cancellationToken: ct);
    }

    private async Task projectDescriptionAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        string description = cmd.ArgumentText.Trim();
        UserSession session = await _store.GetAsync(userId, ct);
        if (session.ActiveProjectId is not null)
        {
            string effective = await _servers.SetProjectDescriptionAsync(userId, description, ct);
            await _bot.SendMessage(
                chatId,
                Replies.ProjectDescriptionSet(effective),
                cancellationToken: ct);
            return;
        }
        // Held locally until /create saves it.
        if (!session.HasImage)
        {
            await _bot.SendMessage(chatId, Replies.NoImageToDescribe(), cancellationToken: ct);
            return;
        }
        await _store.SaveAsync(session with { ActiveProjectDescription = description }, ct);
        await _bot.SendMessage(
            chatId,
            Replies.DescriptionHeld(description),
            cancellationToken: ct);
    }

    private async Task blankColorAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        if (cmd.Args.Count == 0)
        {
            string cur = await _servers.GetProjectBlankColorAsync(userId, ct);
            await _bot.SendMessage(
                chatId,
                Replies.BlankColorCurrent(cur),
                cancellationToken: ct);
            return;
        }
        string effective = await _servers.SetProjectBlankColorAsync(userId, cmd.Args[0], ct);
        await _bot.SendMessage(
            chatId,
            Replies.BlankColorSet(effective),
            cancellationToken: ct);
    }

    // /expire custom (the Custom… button) arms a one-shot free-text prompt consumed by
    // UpdateRouter.
    private async Task expireAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(userId, ct);
        if (session.ActiveProjectId is null)
        {
            await _bot.SendMessage(chatId, Replies.ExpiryNeedsProject(), cancellationToken: ct);
            return;
        }
        if (cmd.ArgumentText.Length == 0)
        {
            await _bot.SendMessage(chatId, Replies.ExpiryPrompt(session.ActiveProjectExpiresAt), replyMarkup: Keyboards.ExpirationMenu(), cancellationToken: ct);
            return;
        }
        if (cmd.ArgumentText.Equals("custom", StringComparison.OrdinalIgnoreCase))
        {
            await _store.SaveAsync(session with { PendingInput = PendingInputs.EXPIRY_DURATION }, ct);
            await _bot.SendMessage(chatId, Replies.ExpiryCustomAsk(), cancellationToken: ct);
            return;
        }
        if (!DurationParser.TryParse(cmd.ArgumentText, out ParsedDuration duration, out bool clear))
        {
            await _bot.SendMessage(chatId, Replies.ExpireUsage(), cancellationToken: ct);
            return;
        }
        long expiresAt = clear ? 0 : duration.From(DateTimeOffset.UtcNow).ToUnixTimeMilliseconds();
        long effective = await _servers.SetProjectExpiryAsync(userId, expiresAt, ct);
        await _bot.SendMessage(chatId, Replies.ExpirySet(effective, duration), cancellationToken: ct);
    }

    // Never on a single tap: only /delete confirm deletes. The working image is kept so it can be
    // re-saved elsewhere.
    private async Task deleteProjectAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(userId, ct);
        if (session.ActiveProjectId is null)
        {
            await _bot.SendMessage(chatId, Replies.DeleteNeedsProject(), cancellationToken: ct);
            return;
        }
        if (!cmd.ArgumentText.Equals("confirm", StringComparison.OrdinalIgnoreCase))
        {
            string name = session.ActiveProjectName ?? session.ActiveProjectId;
            await _bot.SendMessage(
                chatId,
                Replies.DeleteConfirmPrompt(name, session.ActiveServerUrl),
                replyMarkup: Keyboards.DeleteConfirmMenu(),
                cancellationToken: ct);
            return;
        }
        string removed = await _servers.DeleteActiveProjectAsync(userId, ct);
        _sync.Disable(userId);
        await _bot.SendMessage(
            chatId,
            // Already opens with its own glyph — Tag leaves it alone rather than stacking ✅ on 🗑.
            Replies.Tag(Replies.Tone.SUCCESS, Replies.ProjectRemoved(removed)),
            replyMarkup: Keyboards.MainMenu(),
            cancellationToken: ct);
    }
}
