using System.Collections.Concurrent;
using System.Text;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Llm;

public sealed partial class PromptService
{
    // The §7 image-replay rule: only the most recent prior image survives; an edgeMap rides only
    // the current turn.
    public LlmChatRequest BuildTurn(
        long userId, UserSession session, string text, LlmImage? image, LlmImage? edgeMap = null,
        IReadOnlyList<ServerProjectInfo>? projects = null)
    {
        List<LlmMessage> history = snapshotHistory(userId);
        int lastWithImage = history.FindLastIndex(m => m.Images.Count > 0);
        List<LlmMessage> messages = new(history.Count + 1);
        for (int i = 0; i < history.Count; i++)
        {
            LlmMessage message = history[i];
            messages.Add(i == lastWithImage
                ? message with { Images = [message.Images[^1]] }
                : message with { Images = [] });
        }
        List<LlmImage> images = image is null ? [] : edgeMap is null ? [image] : [image, edgeMap];
        messages.Add(new LlmMessage(LlmMessage.RoleUser, text, images));
        LlmOptions options = optionsFor(session);
        (string? serverUrl, string? serverToken) = resolveServer(session, options);
        string system = ChatSystemPrompt + contextSuffix(session, projects);
        if (image is not null && edgeMap is not null)
        {
            system += " " + EdgeMapSentence;
        }
        return new LlmChatRequest
        {
            System = system,
            Messages = messages,
            ServerUrl = serverUrl,
            ServerToken = serverToken,
            Options = options,
        };
    }

    // §7: the working image through the CLI's contour filter. Null silently — the turn must never
    // fail on it.
    private async Task<LlmImage?> buildEdgeMapAsync(long userId, UserSession session, LlmImage? image, CancellationToken ct)
    {
        if (image is null || _attachments is null || session.OriginalImagePath is null)
        {
            return null;
        }
        try
        {
            RenderResult render = await _editing.RenderContourAsync(userId, session.OriginalImagePath, ct);
            return await _attachments.LoadAsync(render.Path, ct);
        }
        catch (Exception)
        {
            return null;
        }
    }

    // An explicit STENCIL_LLM_SERVER_URL wins (reusing the user's stored token), else the first
    // connected server.
    private (string? Url, string? Token) resolveServer(UserSession session, LlmOptions options)
    {
        if (options.Provider != LlmOptions.ProviderStencilServer)
        {
            return (null, null);
        }
        if (options.ServerUrl is string configured && configured.Trim().Length > 0)
        {
            // Normalise like ServerService stores, so a bare-host or trailing-slash configured URL
            // still finds its token.
            string url = _servers.NormalizeUrl(configured);
            ServerConnectionInfo? match = session.Connections
                .FirstOrDefault(c => string.Equals(c.Url, url, StringComparison.OrdinalIgnoreCase));
            // An empty bearer would come back as the server's bare "missing or invalid token",
            // which reads as a bot bug.
            string token = match?.Token is string own && own.Length > 0 ? own : options.ServerToken;
            if (token.Length == 0)
            {
                throw new InvalidOperationException(
                    $"The AI assistant runs through {url} — /connect {url} <token> first.");
            }
            return (url, token);
        }
        ServerConnectionInfo? first = session.Connections.FirstOrDefault();
        if (first is null)
        {
            throw new InvalidOperationException(
                "The AI assistant runs through a Stencil server — /connect <url> first.");
        }
        return (first.Url, first.Token);
    }

    // The cli console's cap.
    public const int MaxContextProjects = 20;

    // Best-effort: null omits the line (the cli's unreachable rule); a turn must never fail on
    // this.
    private async Task<IReadOnlyList<ServerProjectInfo>?> listContextProjectsAsync(
        long userId, UserSession session, CancellationToken ct)
    {
        if (_projects is null || session.Connections.Count == 0)
        {
            return null;
        }
        try
        {
            return await _projects.ListProjectsAsync(userId, null, ct);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            return null;
        }
    }

    // The short dynamic suffix §4 allows, plus the bot's §10 connections line (the cli console's
    // rule).
    private static string contextSuffix(UserSession session, IReadOnlyList<ServerProjectInfo>? projects = null)
    {
        string suffix = session.HasImage
            ? $"\n\nCurrent working image: {session.OriginalWidth}x{session.OriginalHeight} pixels."
            : "\n\nThere is no working image loaded yet.";
        if (session.VideoSourcePath is not null)
        {
            suffix += " The current input is a video, so \"frame\" ops are valid.";
        }
        return suffix + penSuffix(session) + connectionsSuffix(session) + projectsSuffix(projects);
    }

    private static string penSuffix(UserSession session)
    {
        LineStyle pen = session.Edits.Pen;
        return "\n\nPen defaults for new lines: color " + pen.Color
            + ", thickness " + pen.Thickness.ToString(System.Globalization.CultureInfo.InvariantCulture)
            + ", point size " + pen.PointSize.ToString(System.Globalization.CultureInfo.InvariantCulture)
            + $", style {pen.Style}, fill {pen.FillColor}."
            + $" Pending edits: {session.EditHistory.Count} undoable step(s), {session.EditRedo.Count} redoable.";
    }

    // URLs only — a stored token NEVER enters the prompt.
    private static string connectionsSuffix(UserSession session)
    {
        if (session.Connections.Count == 0)
        {
            return "\n\nThe user has no collaboration-server connections (one is added with /connect <url>).";
        }
        string suffix = "\n\nThe user's connected collaboration servers: "
            + string.Join(", ", session.Connections.Select(static c => c.Url)) + ".";
        return suffix + (session.ActiveProjectId is not null && session.ActiveServerUrl is not null
            ? $" Active server project: \"{session.ActiveProjectName}\" on {session.ActiveServerUrl}."
            : " No active server project.");
    }

    // At most MaxContextProjects names per server, then "+N more" (the cli console's rule).
    private static string projectsSuffix(IReadOnlyList<ServerProjectInfo>? projects)
    {
        if (projects is null)
        {
            return "";
        }
        StringBuilder sb = new();
        foreach (IGrouping<string, ServerProjectInfo> server in projects.GroupBy(static p => p.ServerUrl))
        {
            List<string> names = server.Select(static p => p.Record.Name).ToList();
            sb.Append($"\nProjects on {server.Key}: ");
            int shown = Math.Min(names.Count, MaxContextProjects);
            sb.Append(string.Join(", ", names.Take(shown)));
            if (names.Count > shown)
            {
                sb.Append($" (+{names.Count - shown} more)");
            }
            sb.Append('.');
        }
        return sb.ToString();
    }
}
