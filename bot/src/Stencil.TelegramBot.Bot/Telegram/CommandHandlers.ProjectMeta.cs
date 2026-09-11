using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Sessions;
using Telegram.Bot;

namespace Stencil.TelegramBot.Bot.Telegram;

// CommandHandlers — a project's own fields: colour, name, description, blank colour, expiry
// and removal. Class doc lives in CommandHandlers.cs.
public sealed partial class CommandHandlers
{
    private async Task ProjectColorAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
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

    private async Task ProjectNameAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        // Free text: the whole remainder is the name (project names may contain spaces).
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
        // No server project yet — just relabel the local working image (the /create default name).
        if (!session.HasImage)
        {
            await _bot.SendMessage(chatId, "No working image to name — upload a photo or use /blank first.", cancellationToken: ct);
            return;
        }
        await _store.SaveAsync(session with { ImageLabel = name }, ct);
        await _bot.SendMessage(chatId, $"Working image renamed to: {name} — /create will save it under this name.", cancellationToken: ct);
    }

    private async Task ProjectDescriptionAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        // Free text: the whole remainder is the description; empty clears it.
        string description = cmd.ArgumentText.Trim();
        UserSession session = await _store.GetAsync(userId, ct);
        // A saved server project writes through to the server (version-guarded, broadcast to peers).
        if (session.ActiveProjectId is not null)
        {
            string effective = await _servers.SetProjectDescriptionAsync(userId, description, ct);
            await _bot.SendMessage(
                chatId,
                effective.Length == 0 ? "Project description cleared." : $"Project description set:\n{effective}",
                cancellationToken: ct);
            return;
        }
        // No server project yet — hold the description locally (uploaded when /create saves it).
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

    private async Task BlankColorAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
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

    /// <summary>
    /// Set the active project's expiry: <c>/expire &lt;amount|never&gt;</c>; bare shows the picker.
    /// <c>/expire custom</c> (the Custom… button) arms a one-shot free-text prompt consumed by
    /// <see cref="UpdateRouter"/>.
    /// </summary>
    private async Task ExpireAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
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
        // The Custom… button arms a free-text prompt; the next plain message is parsed as a duration.
        if (cmd.ArgumentText.Equals("custom", StringComparison.OrdinalIgnoreCase))
        {
            await _store.SaveAsync(session with { PendingInput = PendingInputs.ExpiryDuration }, ct);
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

    /// <summary>
    /// Remove the active project from its server: bare <c>/delete</c> asks for confirmation;
    /// <c>/delete confirm</c> deletes, clears the active project and turns live sync off — never
    /// on a single tap. The working image is kept so it can be re-saved elsewhere.
    /// </summary>
    private async Task DeleteProjectAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
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
            Replies.Tag(Replies.Tone.Success, $"🗑 Removed '{removed}' from the server."),
            replyMarkup: Keyboards.MainMenu(),
            cancellationToken: ct);
    }
}
