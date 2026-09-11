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

// PromptService — request assembly: BuildTurn, the edge map, server resolution for the
// stencil-server provider, and the §4 context suffix. Class doc lives in PromptService.cs.
public sealed partial class PromptService
{
    /// <summary>
    /// Assemble one chat request: system prompt + context suffix, the replayed history with the
    /// image-replay rule applied (only the most recent prior image survives), then the current
    /// user message. An <paramref name="edgeMap"/> rides only the current turn, never history.
    /// </summary>
    public LlmChatRequest BuildTurn(
        long userId, UserSession session, string text, LlmImage? image, LlmImage? edgeMap = null,
        IReadOnlyList<ServerProjectInfo>? projects = null)
    {
        List<LlmMessage> history = SnapshotHistory(userId);
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
        LlmOptions options = OptionsFor(session);
        (string? serverUrl, string? serverToken) = ResolveServer(session, options);
        string system = ChatSystemPrompt + ContextSuffix(session, projects);
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

    /// <summary>
    /// The §7 edge map: the working image run through the CLI's <c>contour</c> filter and
    /// loaded under the same downscale/size rules as the snapshot. Null (silently — the turn
    /// must never fail on it) when no image is attached this turn or the render/attach fails.
    /// </summary>
    private async Task<LlmImage?> BuildEdgeMapAsync(long userId, UserSession session, LlmImage? image, CancellationToken ct)
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

    /// <summary>
    /// For the <c>stencil-server</c> provider, resolve which server proxies the call: an
    /// explicit <c>STENCIL_LLM_SERVER_URL</c> wins (reusing the user's stored token for that
    /// origin when they have one), otherwise the user's first connected server.
    /// </summary>
    private (string? Url, string? Token) ResolveServer(UserSession session, LlmOptions options)
    {
        if (options.Provider != LlmOptions.ProviderStencilServer)
        {
            return (null, null);
        }
        if (options.ServerUrl is string configured && configured.Trim().Length > 0)
        {
            // Session connections are keyed by the factory's normalised origin (that's what
            // ServerService stores), so normalise the configured URL the same way — a bare-host
            // or trailing-slash STENCIL_LLM_SERVER_URL still finds its stored token.
            string url = _servers.NormalizeUrl(configured);
            ServerConnectionInfo? match = session.Connections
                .FirstOrDefault(c => string.Equals(c.Url, url, StringComparison.OrdinalIgnoreCase));
            // The user's own token first, then the operator's STENCIL_LLM_SERVER_TOKEN. With
            // neither, say the step the user can take — an empty bearer would come back as the
            // server's bare "missing or invalid token", which reads as a bot bug.
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
            // Names the one step the user can take; how the bot is configured stays out of chat.
            throw new InvalidOperationException(
                "The AI assistant runs through a Stencil server — /connect <url> first.");
        }
        return (first.Url, first.Token);
    }

    /// <summary>How many project names one server contributes to the context suffix (the cli console's cap).</summary>
    public const int MaxContextProjects = 20;

    /// <summary>
    /// The per-connection project listings for the context suffix, best-effort: null (line
    /// omitted, the cli's unreachable rule) when the bot has no server service, the user has
    /// no connections, or the listing call fails. A turn must never fail on this.
    /// </summary>
    private async Task<IReadOnlyList<ServerProjectInfo>?> ListContextProjectsAsync(
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

    /// <summary>
    /// The short dynamic suffix §4 allows: working image dimensions / video-ness, the pen
    /// defaults and pending-edit stack size, plus the bot's §10 connections line and a capped
    /// per-server project-name listing (the cli console's rule).
    /// </summary>
    private static string ContextSuffix(UserSession session, IReadOnlyList<ServerProjectInfo>? projects = null)
    {
        string suffix = session.HasImage
            ? $"\n\nCurrent working image: {session.OriginalWidth}x{session.OriginalHeight} pixels."
            : "\n\nThere is no working image loaded yet.";
        if (session.VideoSourcePath is not null)
        {
            suffix += " The current input is a video, so \"frame\" ops are valid.";
        }
        return suffix + PenSuffix(session) + ConnectionsSuffix(session) + ProjectsSuffix(projects);
    }

    private static string PenSuffix(UserSession session)
    {
        LineStyle pen = session.Edits.Pen;
        return "\n\nPen defaults for new lines: color " + pen.Color
            + ", thickness " + pen.Thickness.ToString(System.Globalization.CultureInfo.InvariantCulture)
            + ", point size " + pen.PointSize.ToString(System.Globalization.CultureInfo.InvariantCulture)
            + $", style {pen.Style}, fill {pen.FillColor}."
            + $" Pending edits: {session.EditHistory.Count} undoable step(s), {session.EditRedo.Count} redoable.";
    }

    /// <summary>
    /// The §10 connections line: the user's connected server URLs and the active project, so
    /// "what am I connected to?" is answered in the reply without ops. URLs only — a stored
    /// token NEVER enters the prompt.
    /// </summary>
    private static string ConnectionsSuffix(UserSession session)
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

    /// <summary>
    /// The capped per-server project-name listing (the cli console's rule: at most
    /// <see cref="MaxContextProjects"/> names per server, then a "+N more"), so "what's on my
    /// server?" is answered in the reply without ops. Null listings (unreachable) omit the line.
    /// </summary>
    private static string ProjectsSuffix(IReadOnlyList<ServerProjectInfo>? projects)
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
