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
            await _bot.SendMessage(chatId, "Usage: /project-color <#hex|name|clear>, e.g. /project-color #ff5623", cancellationToken: ct);
            return;
        }
        string arg = cmd.Args[0];
        string color = arg is "clear" or "none" or "default" ? "" : arg;
        string effective = await _servers.SetProjectColorAsync(userId, color, ct);
        await _bot.SendMessage(
            chatId,
            effective.Length == 0 ? "Project colour cleared." : $"Project colour set to {effective} {Replies.ColorDot(effective)}",
            cancellationToken: ct);
    }

    private async Task projectNameAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        // Project names may contain spaces.
        string name = cmd.ArgumentText.Trim();
        if (name.Length == 0)
        {
            await _bot.SendMessage(chatId, "Usage: /project-name <new name>, e.g. /project-name Poster draft", cancellationToken: ct);
            return;
        }
        UserSession session = await _store.GetAsync(userId, ct);
        // A saved server project renames on the server (version-guarded, broadcast to peers).
        if (session.ActiveProjectId is not null)
        {
            string effective = await _servers.SetProjectNameAsync(userId, name, ct);
            await _bot.SendMessage(chatId, $"Project renamed to: {effective}", cancellationToken: ct);
            return;
        }
        // No server project yet: relabel the local working image (the /create default name).
        if (!session.HasImage)
        {
            await _bot.SendMessage(chatId, "No working image to name — upload a photo or use /blank first.", cancellationToken: ct);
            return;
        }
        await _store.SaveAsync(session with { ImageLabel = name }, ct);
        await _bot.SendMessage(chatId, $"Working image renamed to: {name} — /create will save it under this name.", cancellationToken: ct);
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
                effective.Length == 0 ? "Project description cleared." : $"Project description set:\n{effective}",
                cancellationToken: ct);
            return;
        }
        // Held locally until /create saves it.
        if (!session.HasImage)
        {
            await _bot.SendMessage(chatId, "No working image to describe — upload a photo or use /blank first.", cancellationToken: ct);
            return;
        }
        await _store.SaveAsync(session with { ActiveProjectDescription = description }, ct);
        await _bot.SendMessage(
            chatId,
            description.Length == 0
                ? "Description cleared."
                : $"Description set (saved with the project on /create):\n{description}",
            cancellationToken: ct);
    }

    private async Task blankColorAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        if (cmd.Args.Count == 0)
        {
            string cur = await _servers.GetProjectBlankColorAsync(userId, ct);
            await _bot.SendMessage(
                chatId,
                cur.Length == 0 ? "This project is not a blank image." : $"Blank colour: {cur} {Replies.ColorDot(cur)}",
                cancellationToken: ct);
            return;
        }
        string effective = await _servers.SetProjectBlankColorAsync(userId, cmd.Args[0], ct);
        await _bot.SendMessage(
            chatId,
            effective.Length == 0 ? "This project is not a blank image — nothing to recolour." : $"Blank colour set to {effective} {Replies.ColorDot(effective)}",
            cancellationToken: ct);
    }

    // /expire custom (the Custom… button) arms a one-shot free-text prompt consumed by
    // UpdateRouter.
    private async Task expireAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(userId, ct);
        if (session.ActiveProjectId is null)
        {
            await _bot.SendMessage(chatId, "Open a server project first (/fetch or /create), then set an expiry.", cancellationToken: ct);
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
            await _bot.SendMessage(
                chatId,
                "Send a custom expiry, e.g. \"3 days\", \"week\", \"2 weeks\", \"1 month\", or \"week 4\".",
                cancellationToken: ct);
            return;
        }
        if (!DurationParser.TryParse(cmd.ArgumentText, out ParsedDuration duration, out bool clear))
        {
            await _bot.SendMessage(chatId, Replies.ExpireUsage(), cancellationToken: ct);
            return;
        }
        long expiresAt = clear ? 0 : duration.From(DateTimeOffset.UtcNow).ToUnixTimeMilliseconds();
        long effective = await _servers.SetProjectExpiryAsync(userId, expiresAt, ct);
        string message = effective <= 0
            ? "Expiry cleared — this project is kept forever."
            : $"Expiry set to {Replies.FmtDate(effective)} ({duration} from now).";
        await _bot.SendMessage(chatId, message, cancellationToken: ct);
    }

    // Never on a single tap: only /delete confirm deletes. The working image is kept so it can be
    // re-saved elsewhere.
    private async Task deleteProjectAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(userId, ct);
        if (session.ActiveProjectId is null)
        {
            await _bot.SendMessage(chatId, "No active server project to remove — /fetch or /create one first.", cancellationToken: ct);
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
            Replies.Tag(Replies.Tone.SUCCESS, $"🗑 Removed '{removed}' from the server."),
            replyMarkup: Keyboards.MainMenu(),
            cancellationToken: ct);
    }
}
