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

// CommandHandlers — server/project commands: /start deep links, connections,
// project listing/fetch/create/save/sync, and project metadata (color, name,
// description, blank color, expiry, delete). Class doc lives in CommandHandlers.cs.
public sealed partial class CommandHandlers
{
    /// <summary>
    /// Greet the user — or, when the message carries a deep-link start payload (a
    /// t.me/&lt;bot&gt;?start=&lt;payload&gt; link from the browser/desktop "Open in…"), connect to
    /// the referenced server like a fresh client and open the project.
    /// </summary>
    private async Task StartAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        if (cmd.ArgumentText.Length > 0
            && DeepLinkCodec.TryDecode(cmd.ArgumentText, out string serverUrl, out string projectId))
        {
            await OpenDeepLinkedProjectAsync(userId, chatId, serverUrl, projectId, ct);
            return;
        }
        await _bot.SendMessage(
            chatId,
            "Welcome to Stencil. Send a photo to start editing, or tap a button below.",
            replyMarkup: Keyboards.MainMenu(),
            cancellationToken: ct);
    }

    /// <summary>
    /// Open a deep-linked server project: reuse the session's connection to that origin, else
    /// connect tokenless (the server mints one — no token ever rides the link), then fetch and
    /// render. Failures reply with the manual /connect + /fetch recipe.
    /// </summary>
    private async Task OpenDeepLinkedProjectAsync(long userId, long chatId, string serverUrl,
        string projectId, CancellationToken ct)
    {
        try
        {
            UserSession session = await _store.GetAsync(userId, ct);
            if (session.FindConnection(serverUrl) is null)
            {
                await _servers.ConnectAsync(userId, serverUrl, token: null, !_options.TlsInsecure, ct);
            }
            UserSession updated = await _servers.FetchAsync(userId, projectId, serverUrl, ct);
            await _bot.SendMessage(
                chatId,
                Replies.Tag(Replies.Tone.Success,
                    $"Loaded shared project '{updated.ActiveProjectName}' from {serverUrl}."),
                cancellationToken: ct);
            await RenderAndSendAsync(userId, chatId, ct, mutating: false);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            await _bot.SendMessage(
                chatId,
                Replies.Tag(Replies.Tone.Error,
                    $"Couldn't open the shared project — {ex.Message}\n\n"
                    + $"Try manually:\n/connect {serverUrl} [token]\n/fetch {projectId}"),
                cancellationToken: ct);
        }
    }

    /// <summary>
    /// Hand the active project to the desktop app: <c>/link</c> replies with an https link that
    /// bounces through the browser app's <c>launch.html</c> to <c>stencil://open?…</c>. Server
    /// projects only (a link carries a reference, never image bytes), and no token rides it —
    /// the recipient connects to that server with their own credential.
    /// </summary>
    private async Task LinkAsync(long userId, long chatId, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(userId, ct);
        if (session.ActiveProjectId is null || session.ActiveServerUrl is null)
        {
            await _bot.SendMessage(
                chatId,
                "No active server project to link — /fetch or /create one first (a link points at a "
                + "server project, it can't carry the image itself).",
                cancellationToken: ct);
            return;
        }
        string? url = DesktopLinkBuilder.TryProjectBounceUrl(
            _options.BrowserAppUrl, session.ActiveServerUrl, session.ActiveProjectId,
            session.ActiveProjectVersion);
        if (url is null)
        {
            await _bot.SendMessage(chatId, Replies.DesktopLinkUnusable(), cancellationToken: ct);
            return;
        }
        await _bot.SendMessage(
            chatId,
            Replies.DesktopLink(
                session.ActiveProjectName ?? session.ActiveProjectId, url,
                DesktopLinkBuilder.IsLoopbackBase(_options.BrowserAppUrl)),
            cancellationToken: ct);
    }

    private async Task ConnectAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        if (cmd.Args.Count == 0)
        {
            await _bot.SendMessage(chatId, Replies.ConnectUsage(), cancellationToken: ct);
            return;
        }
        string url = cmd.Args[0];
        string? token = cmd.Args.Count > 1 ? cmd.Args[1] : null;
        bool verifyTls = !_options.TlsInsecure;
        ServerConnectionInfo info = await _servers.ConnectAsync(userId, url, token, verifyTls, ct);
        await _bot.SendMessage(
            chatId,
            Replies.Tag(Replies.Tone.Success, $"Connected to {info.Url}."),
            replyMarkup: Keyboards.MainMenu(),
            cancellationToken: ct);
    }

    private async Task DisconnectAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        string? url = cmd.Args.Count == 0 ? null : cmd.Args[0];
        bool removed = await _servers.DisconnectAsync(userId, url, ct);
        string text = removed
            ? Replies.Tag(Replies.Tone.Success, "Disconnected.")
            : Replies.Tag(Replies.Tone.Notice, "No matching connection to disconnect.");
        await _bot.SendMessage(chatId, text, cancellationToken: ct);
    }

    private async Task ConnectionsAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        string filter = cmd.Args.Count == 0 ? "" : cmd.Args[0].ToLowerInvariant();
        if (filter is not ("" or "admin" or "session"))
        {
            await _bot.SendMessage(chatId, Replies.ConnectionsUsage(), cancellationToken: ct);
            return;
        }
        IReadOnlyList<ServerConnectionInfo> connections = await _servers.ConnectionsAsync(userId, ct);
        if (filter.Length > 0)
        {
            bool wantAdmin = filter == "admin";
            connections = connections
                .Where(c => (c.CredentialKind == CredentialKind.Admin) == wantAdmin)
                .ToList();
        }
        await _bot.SendMessage(chatId, Replies.ConnectionsText(connections, filter), cancellationToken: ct);
    }

    private async Task ProjectsAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        string? url = cmd.Args.Count == 0 ? null : cmd.Args[0];
        IReadOnlyList<ServerProjectInfo> projects = await _servers.ListProjectsAsync(userId, url, ct);
        if (projects.Count == 0)
        {
            await _bot.SendMessage(chatId, Replies.ProjectsText(projects), cancellationToken: ct);
            return;
        }
        await _bot.SendMessage(
            chatId,
            Replies.ProjectsText(projects),
            replyMarkup: Keyboards.ProjectList(projects),
            cancellationToken: ct);
    }

    private async Task FetchAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        if (cmd.ArgumentText.Length == 0)
        {
            // Bare /fetch: list the fetchable projects (as tappable buttons) plus the usage hint.
            IReadOnlyList<ServerProjectInfo> projects = await _servers.ListProjectsAsync(userId, null, ct);
            string text = $"{Replies.ProjectsText(projects)}\n\nUsage: /fetch <project name or id>";
            await _bot.SendMessage(
                chatId,
                text,
                replyMarkup: projects.Count == 0 ? null : Keyboards.ProjectList(projects),
                cancellationToken: ct);
            return;
        }
        UserSession session = await _servers.FetchAsync(userId, cmd.ArgumentText, null, ct);
        // Contract §12: with chat saving on, a fetched project brings its persisted chat back —
        // the assistant's history is seeded from it (restoring never triggers a model call).
        int restored = await TryRestoreChatAsync(userId, session, ct);
        string loaded = Replies.Tag(Replies.Tone.Success, $"Loaded project '{session.ActiveProjectName}'.");
        if (restored > 0)
        {
            loaded += "\n" + Replies.ChatRestored(restored);
        }
        await _bot.SendMessage(chatId, loaded, cancellationToken: ct);
        await RenderAndSendAsync(userId, chatId, ct, mutating: false);
    }

    private async Task CreateAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        string? name = cmd.ArgumentText.Length == 0 ? null : cmd.ArgumentText;
        ProjectRecord record = await _servers.CreateProjectAsync(userId, name, null, ct);
        await _bot.SendMessage(
            chatId,
            Replies.Tag(Replies.Tone.Success,
                $"Created project '{record.Name}' (id {record.Id}, v{record.Version})."),
            cancellationToken: ct);
    }

    private async Task SaveAsync(long userId, long chatId, CancellationToken ct)
    {
        ProjectRecord record = await _servers.SaveActiveProjectAsync(userId, ct);
        await _bot.SendMessage(
            chatId,
            Replies.Tag(Replies.Tone.Success, $"Saved '{record.Name}' (v{record.Version})."),
            cancellationToken: ct);
    }

    private async Task SyncAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(userId, ct);
        bool target = cmd.Args.Count == 0
            ? !session.SyncEnabled
            : cmd.Args[0] is "on" or "true" or "1" or "yes";
        if (target && session.ActiveProjectId is null)
        {
            await _bot.SendMessage(chatId, "Open a server project first (/fetch or /create), then /sync on.", cancellationToken: ct);
            return;
        }
        await _store.SaveAsync(session with { SyncEnabled = target }, ct);
        if (target)
        {
            _sync.Enable(userId, chatId);
            await _bot.SendMessage(chatId, "🔄 Live sync ON — your edits upload automatically, and a peer's changes are pulled into the chat.", cancellationToken: ct);
        }
        else
        {
            _sync.Disable(userId);
            await _bot.SendMessage(chatId, "Live sync OFF. Use /save to push changes manually.", cancellationToken: ct);
        }
    }

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
