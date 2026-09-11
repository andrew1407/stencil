using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Sessions;
using Telegram.Bot;

namespace Stencil.TelegramBot.Bot.Telegram;

// CommandHandlers — the project list and what moves a project between the server and the
// working image: /projects, /fetch, /create, /save, /sync. Class doc lives in CommandHandlers.cs.
public sealed partial class CommandHandlers
{
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
}
