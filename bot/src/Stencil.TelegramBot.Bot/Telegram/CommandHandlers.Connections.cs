using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Links;
using Telegram.Bot;

namespace Stencil.TelegramBot.Bot.Telegram;

public sealed partial class CommandHandlers
{
    // A t.me/<bot>?start=<payload> link from the browser/desktop "Open in…" connects and opens the
    // project.
    private async Task startAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        if (cmd.ArgumentText.Length > 0
            && DeepLinkCodec.TryDecode(cmd.ArgumentText, out string serverUrl, out string projectId))
        {
            await openDeepLinkedProjectAsync(userId, chatId, serverUrl, projectId, ct);
            return;
        }
        await _bot.SendMessage(
            chatId,
            "Welcome to Stencil. Send a photo to start editing, or tap a button below.",
            replyMarkup: Keyboards.MainMenu(),
            cancellationToken: ct);
    }

    // Connect tokenless when no connection exists (no token ever rides the link); failures reply
    // with the manual recipe.
    private async Task openDeepLinkedProjectAsync(long userId, long chatId, string serverUrl,
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
                Replies.Tag(Replies.Tone.SUCCESS,
                    $"Loaded shared project '{updated.ActiveProjectName}' from {serverUrl}."),
                cancellationToken: ct);
            await RenderAndSendAsync(userId, chatId, ct, mutating: false);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            await _bot.SendMessage(
                chatId,
                Replies.Tag(Replies.Tone.ERROR,
                    $"Couldn't open the shared project — {ex.Message}\n\n"
                    + $"Try manually:\n/connect {serverUrl} [token]\n/fetch {projectId}"),
                cancellationToken: ct);
        }
    }

    // Server projects only (a link carries a reference, never image bytes), and no token rides it.
    private async Task linkAsync(long userId, long chatId, CancellationToken ct)
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

    private async Task connectAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
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
            Replies.Tag(Replies.Tone.SUCCESS, $"Connected to {info.Url}."),
            replyMarkup: Keyboards.MainMenu(),
            cancellationToken: ct);
    }

    private async Task disconnectAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        string? url = cmd.Args.Count == 0 ? null : cmd.Args[0];
        bool removed = await _servers.DisconnectAsync(userId, url, ct);
        string text = removed
            ? Replies.Tag(Replies.Tone.SUCCESS, "Disconnected.")
            : Replies.Tag(Replies.Tone.NOTICE, "No matching connection to disconnect.");
        await _bot.SendMessage(chatId, text, cancellationToken: ct);
    }

    private async Task connectionsAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
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
                .Where(c => (c.CredentialKind == CredentialKind.ADMIN) == wantAdmin)
                .ToList();
        }
        await _bot.SendMessage(chatId, Replies.ConnectionsText(connections, filter), cancellationToken: ct);
    }
}
