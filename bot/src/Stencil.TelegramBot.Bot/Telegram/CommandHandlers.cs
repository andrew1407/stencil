using System.Globalization;
using System.Text;
using Microsoft.Extensions.Logging;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Links;
using Telegram.Bot;
using Telegram.Bot.Types;
using Telegram.Bot.Types.Enums;

namespace Stencil.TelegramBot.Bot.Telegram;

/// <summary>
/// One handler per slash command, plus the shared render-and-send helper. Each command folds its
/// intent through <see cref="IEditingService"/> / <see cref="IServerService"/> — the same
/// Application services the callback buttons use — then replies over Telegram.
/// </summary>
public sealed class CommandHandlers
{
    private readonly IEditingService _editing;
    private readonly IServerService _servers;
    private readonly ISessionStore _store;
    private readonly ITelegramBotClient _bot;
    private readonly BotOptions _options;
    private readonly SyncRegistry _sync;
    private readonly LayoutFetcher _layoutFetcher;
    private readonly ILogger<CommandHandlers> _logger;

    public CommandHandlers(
        IEditingService editing,
        IServerService servers,
        ISessionStore store,
        ITelegramBotClient bot,
        BotOptions options,
        SyncRegistry sync,
        LayoutFetcher layoutFetcher,
        ILogger<CommandHandlers> logger)
    {
        _editing = editing;
        _servers = servers;
        _store = store;
        _bot = bot;
        _options = options;
        _sync = sync;
        _layoutFetcher = layoutFetcher;
        _logger = logger;
    }

    /// <summary>Route a parsed command to its handler (unknown verbs fall back to a /help hint).</summary>
    public Task DispatchAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct) =>
        cmd.Verb switch
        {
            "start" => StartAsync(userId, chatId, cmd, ct),
            "help" => HelpAsync(chatId, ct),
            "connect" => ConnectAsync(userId, chatId, cmd, ct),
            "disconnect" => DisconnectAsync(userId, chatId, cmd, ct),
            "connections" => ConnectionsAsync(userId, chatId, ct),
            "projects" => ProjectsAsync(userId, chatId, cmd, ct),
            "fetch" => FetchAsync(userId, chatId, cmd, ct),
            "create" => CreateAsync(userId, chatId, cmd, ct),
            "save" => SaveAsync(userId, chatId, ct),
            "sync" => SyncAsync(userId, chatId, cmd, ct),
            "projectcolor" or "project_color" or "project-color" or "pcolor" => ProjectColorAsync(userId, chatId, cmd, ct),
            "expire" or "expiry" or "expiration" => ExpireAsync(userId, chatId, cmd, ct),
            "delete" or "remove" or "deleteproject" or "delete_project" => DeleteProjectAsync(userId, chatId, cmd, ct),
            "blank" => BlankAsync(userId, chatId, cmd, ct),
            "format" => FormatAsync(userId, chatId, cmd, ct),
            "url" => UrlAsync(userId, chatId, cmd, ct),
            "frame" => FrameAsync(userId, chatId, cmd, ct),
            "crop" => CropAsync(userId, chatId, cmd, ct),
            "rotate" => RotateAsync(userId, chatId, cmd, ct),
            "filter" => FilterAsync(userId, chatId, cmd, ct),
            "draw" => DrawAsync(userId, chatId, cmd, ct),
            "line" or "polyline" => DrawShapeAsync(userId, chatId, "line", cmd.Args, ct),
            "rect" or "rectangle" => DrawShapeAsync(userId, chatId, "rect", cmd.Args, ct),
            "poly" or "polygon" => DrawShapeAsync(userId, chatId, "poly", cmd.Args, ct),
            "color" or "colour" => PenColorAsync(userId, chatId, cmd, ct),
            "thickness" => PenThicknessAsync(userId, chatId, cmd, ct),
            "markers" or "marker" => PenMarkersAsync(userId, chatId, cmd, ct),
            "style" => PenStyleAsync(userId, chatId, cmd, ct),
            "fill" => PenFillAsync(userId, chatId, cmd, ct),
            "pen" => PenAsync(userId, chatId, ct),
            "undo" => UndoAsync(userId, chatId, ct),
            "redo" => RedoAsync(userId, chatId, ct),
            "undoline" or "undo_line" => UndoLineAsync(userId, chatId, ct),
            "clearlines" or "clear_lines" => ClearLinesAsync(userId, chatId, ct),
            "reset" => ResetAsync(userId, chatId, ct),
            "drop" => DropAsync(userId, chatId, ct),
            "image" => ImageAsync(userId, chatId, ct),
            "layout" => LayoutAsync(userId, chatId, cmd, ct),
            "json" => JsonAsync(userId, chatId, ct),
            "status" => StatusAsync(userId, chatId, ct),
            "cancel" => CancelAsync(chatId, ct),
            _ => UnknownAsync(chatId, ct),
        };

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
                $"Loaded shared project '{updated.ActiveProjectName}' from {serverUrl}.",
                cancellationToken: ct);
            await RenderAndSendAsync(userId, chatId, ct, mutating: false);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            await _bot.SendMessage(
                chatId,
                $"Couldn't open the shared project — {ex.Message}\n\n"
                + $"Try manually:\n/connect {serverUrl} [token]\n/fetch {projectId}",
                cancellationToken: ct);
        }
    }

    /// <summary>
    /// Apply a layout to the working image: <c>/layout &lt;json | http(s) url to a .json&gt;</c> —
    /// the command-line sibling of uploading a .json document. URLs are SSRF-vetted like /url.
    /// </summary>
    private async Task LayoutAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        if (cmd.ArgumentText.Length == 0)
        {
            await _bot.SendMessage(
                chatId,
                "Usage: /layout <layout JSON | link to a layout .json>, e.g. "
                + "/layout {\"imageWidth\":800,\"imageHeight\":600,\"lines\":[…]} — "
                + "or just upload the .json file.",
                cancellationToken: ct);
            return;
        }
        UserSession session = await _store.GetAsync(userId, ct);
        if (!session.HasImage)
        {
            await _bot.SendMessage(chatId, "Upload an image (or use /blank) before applying a layout.", cancellationToken: ct);
            return;
        }
        byte[] bytes;
        bool isUrl = cmd.Args.Count == 1
            && Uri.TryCreate(cmd.Args[0], UriKind.Absolute, out Uri? uri)
            && uri.Scheme is "http" or "https";
        if (isUrl)
        {
            // Same guard as /url: the bot is open to any Telegram user, so reject
            // loopback/private/metadata hosts before fetching.
            await RemoteImageUrl.ValidateAsync(cmd.Args[0], ct);
            byte[]? fetched = await _layoutFetcher.FetchAsync(cmd.Args[0], ct);
            if (fetched is null)
            {
                await _bot.SendMessage(chatId, "Could not fetch the layout from that link.", cancellationToken: ct);
                return;
            }
            bytes = fetched;
        }
        else
        {
            bytes = Encoding.UTF8.GetBytes(cmd.ArgumentText);
        }
        StencilLayout? layout = StencilLayoutParser.Parse(bytes);
        if (layout is null)
        {
            await _bot.SendMessage(chatId, "That isn't a valid Stencil layout JSON.", cancellationToken: ct);
            return;
        }
        await _editing.ApplyLayoutAsync(userId, layout, ct);
        await RenderAndSendAsync(userId, chatId, ct);
    }

    /// <summary>Show the full command help plus the main menu.</summary>
    private Task HelpAsync(long chatId, CancellationToken ct) =>
        _bot.SendMessage(chatId, Replies.HelpText(), replyMarkup: Keyboards.MainMenu(), cancellationToken: ct);

    /// <summary>Connect to a collaboration server: <c>/connect &lt;url&gt; [token]</c>.</summary>
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
            $"Connected to {info.Url}.",
            replyMarkup: Keyboards.MainMenu(),
            cancellationToken: ct);
    }

    /// <summary>Forget a connection: <c>/disconnect [url]</c> (the most recent when omitted).</summary>
    private async Task DisconnectAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        string? url = cmd.Args.Count == 0 ? null : cmd.Args[0];
        bool removed = await _servers.DisconnectAsync(userId, url, ct);
        string text = removed ? "Disconnected." : "No matching connection to disconnect.";
        await _bot.SendMessage(chatId, text, cancellationToken: ct);
    }

    private async Task ConnectionsAsync(long userId, long chatId, CancellationToken ct)
    {
        IReadOnlyList<ServerConnectionInfo> connections = await _servers.ConnectionsAsync(userId, ct);
        await _bot.SendMessage(chatId, Replies.ConnectionsText(connections), cancellationToken: ct);
    }

    /// <summary>List projects (across all servers, or one) as tappable buttons.</summary>
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

    /// <summary>Load a project as the working image: <c>/fetch &lt;name|id&gt;</c> (bare lists them).</summary>
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
        await _bot.SendMessage(chatId, $"Loaded project '{session.ActiveProjectName}'.", cancellationToken: ct);
        await RenderAndSendAsync(userId, chatId, ct, mutating: false);
    }

    /// <summary>Save the current result as a new project: <c>/create [name]</c>.</summary>
    private async Task CreateAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        string? name = cmd.ArgumentText.Length == 0 ? null : cmd.ArgumentText;
        ProjectRecord record = await _servers.CreateProjectAsync(userId, name, null, ct);
        await _bot.SendMessage(
            chatId,
            $"Created project '{record.Name}' (id {record.Id}, v{record.Version}).",
            cancellationToken: ct);
    }

    private async Task SaveAsync(long userId, long chatId, CancellationToken ct)
    {
        ProjectRecord record = await _servers.SaveActiveProjectAsync(userId, ct);
        await _bot.SendMessage(
            chatId,
            $"Saved '{record.Name}' (v{record.Version}).",
            cancellationToken: ct);
    }

    /// <summary>Start a blank canvas: <c>/blank [format] [w h] [color]</c>.</summary>
    private async Task BlankAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        string? page = null;
        int? width = null;
        int? height = null;
        string? color = null;
        IReadOnlyList<string> args = cmd.Args;
        // An optional leading named page format (case-insensitive), e.g. /blank b5 pink.
        if (args.Count >= 1 && PageFormats.TryGet(args[0], out string canonical, out _, out _))
        {
            page = canonical;
            args = args.Skip(1).ToList();
        }
        if (args.Count >= 2 && int.TryParse(args[0], out int w) && int.TryParse(args[1], out int h))
        {
            width = w;
            height = h;
            if (args.Count >= 3)
            {
                color = args[2];
            }
        }
        else if (args.Count >= 1 && !int.TryParse(args[0], out _))
        {
            color = args[0];
        }
        // A format plus explicit dims is rejected by CliArgvBuilder (mutually exclusive).
        BlankSpec spec = new(width, height, color, page);
        await _editing.BlankAsync(userId, spec, ct);
        await RenderAndSendAsync(userId, chatId, ct);
    }

    /// <summary>Show or set the page format: <c>/format [name | custom &lt;w&gt; &lt;h&gt;]</c>.</summary>
    private async Task FormatAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        if (cmd.Args.Count == 0)
        {
            await _bot.SendMessage(chatId, Replies.PageFormatList(), cancellationToken: ct);
            return;
        }
        string first = cmd.Args[0];
        if (first.Equals("custom", StringComparison.OrdinalIgnoreCase))
        {
            if (cmd.Args.Count < 3
                || !double.TryParse(cmd.Args[1], NumberStyles.Float, CultureInfo.InvariantCulture, out double w)
                || !double.TryParse(cmd.Args[2], NumberStyles.Float, CultureInfo.InvariantCulture, out double h)
                || w <= 0 || h <= 0)
            {
                await _bot.SendMessage(chatId, "Usage: /format custom <width> <height> (cm), e.g. /format custom 10 15", cancellationToken: ct);
                return;
            }
            await _editing.SetPageFormatAsync(userId, "custom", w, h, ct);
            await _bot.SendMessage(chatId, $"Page format set to custom ({PageFormats.Cm(w)}×{PageFormats.Cm(h)} cm) — saved into the project layout.", cancellationToken: ct);
            return;
        }
        if (!PageFormats.TryGet(first, out string name, out double wcm, out double hcm))
        {
            await _bot.SendMessage(chatId, $"Unknown page format '{first}' — type /format to list formats.", cancellationToken: ct);
            return;
        }
        await _editing.SetPageFormatAsync(userId, name, null, null, ct);
        await _bot.SendMessage(chatId, $"Page format set to {name} ({PageFormats.Cm(wcm)}×{PageFormats.Cm(hcm)} cm) — the /blank default, saved into the project layout.", cancellationToken: ct);
    }

    /// <summary>Crop the working image: <c>/crop &lt;spec&gt; [album]</c>.</summary>
    private async Task CropAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        string spec = cmd.ArgumentText;
        bool album = false;
        if (cmd.Args.Count > 0 && string.Equals(cmd.Args[^1], "album", StringComparison.OrdinalIgnoreCase))
        {
            album = true;
            int idx = spec.LastIndexOf(cmd.Args[^1], StringComparison.OrdinalIgnoreCase);
            spec = idx >= 0 ? spec[..idx].TrimEnd() : spec;
        }
        if (spec.Length == 0)
        {
            await _bot.SendMessage(chatId, Replies.CropUsage(), cancellationToken: ct);
            return;
        }
        await _editing.SetCropAsync(userId, spec, album, ct);
        await RenderAndSendAsync(userId, chatId, ct);
    }

    /// <summary>Load an image from an http(s) URL: <c>/url &lt;link&gt;</c>.</summary>
    private async Task UrlAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        if (cmd.Args.Count == 0)
        {
            await _bot.SendMessage(chatId, "Usage: /url <http(s) image link>, e.g. /url https://example.com/photo.png", cancellationToken: ct);
            return;
        }
        string url = cmd.Args[0];
        await _editing.SetImageFromUrlAsync(userId, url, LabelFromUrl(url), ct);
        await RenderAndSendAsync(userId, chatId, ct);
    }

    /// <summary>Re-grab a frame from the loaded video: <c>/frame [n]</c> (needs ffmpeg).</summary>
    private async Task FrameAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        int frame = 0;
        if (cmd.Args.Count >= 1 && int.TryParse(cmd.Args[0], out int n))
        {
            frame = n;
        }
        await _editing.ExtractFrameAsync(userId, frame, ct);
        await RenderAndSendAsync(userId, chatId, ct);
    }

    /// <summary>Draw a shape: <c>/draw &lt;line|rect|poly&gt; x1,y1 x2,y2 …</c>.</summary>
    private async Task DrawAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        if (cmd.Args.Count == 0)
        {
            await _bot.SendMessage(chatId, Replies.DrawHelp(), cancellationToken: ct);
            return;
        }
        string shape = cmd.Args[0];
        IReadOnlyList<string> pointTokens = cmd.Args.Skip(1).ToList();
        await DrawShapeAsync(userId, chatId, shape, pointTokens, ct);
    }

    /// <summary>Append a styled line/rectangle/polygon built from the point tokens.</summary>
    private async Task DrawShapeAsync(long userId, long chatId, string shape, IReadOnlyList<string> pointTokens, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(userId, ct);
        if (!session.HasImage)
        {
            await _bot.SendMessage(chatId, "Upload an image (or use /blank) before drawing.", cancellationToken: ct);
            return;
        }
        string kind = shape.ToLowerInvariant();
        bool closed = kind is "rect" or "rectangle" or "poly" or "polygon";
        if (!DrawArguments.TryParsePoints(pointTokens, session.OriginalWidth, session.OriginalHeight, out List<LayoutPoint> points, out string? error))
        {
            await _bot.SendMessage(chatId, $"{error}\n\n{Replies.DrawHelp()}", cancellationToken: ct);
            return;
        }
        IReadOnlyList<LayoutPoint> shapePoints = points;
        if (kind is "rect" or "rectangle")
        {
            if (points.Count != 2)
            {
                await _bot.SendMessage(chatId, "A rectangle needs exactly two corner points: /draw rect x1,y1 x2,y2", cancellationToken: ct);
                return;
            }
            shapePoints = DrawArguments.Rectangle(points[0], points[1]);
        }
        else if (kind is "poly" or "polygon")
        {
            if (points.Count < 3)
            {
                await _bot.SendMessage(chatId, "A polygon needs at least three points.", cancellationToken: ct);
                return;
            }
        }
        else if (kind is "line" or "polyline")
        {
            if (points.Count < 2)
            {
                await _bot.SendMessage(chatId, "A line needs at least two points.", cancellationToken: ct);
                return;
            }
        }
        else
        {
            await _bot.SendMessage(chatId, Replies.DrawHelp(), cancellationToken: ct);
            return;
        }
        await _editing.AddLineAsync(userId, shapePoints, closed, ct);
        await RenderAndSendAsync(userId, chatId, ct);
    }

    /// <summary>Set the pen colour: <c>/color &lt;#hex|name&gt;</c>.</summary>
    private async Task PenColorAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        if (cmd.Args.Count == 0)
        {
            await _bot.SendMessage(chatId, "Usage: /color <#hex|name>, e.g. /color #ff5623", cancellationToken: ct);
            return;
        }
        await _editing.ConfigurePenAsync(userId, color: cmd.Args[0], thickness: null, markerSize: null, style: null, fill: null, ct);
        await _bot.SendMessage(chatId, $"Pen colour set to {cmd.Args[0]}.", cancellationToken: ct);
    }

    /// <summary>Set the pen stroke width: <c>/thickness &lt;n&gt;</c>.</summary>
    private async Task PenThicknessAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        if (!TryParseNonNegative(cmd.Args, out double value))
        {
            await _bot.SendMessage(chatId, "Usage: /thickness <n> (pixels, e.g. 4)", cancellationToken: ct);
            return;
        }
        await _editing.ConfigurePenAsync(userId, color: null, thickness: value, markerSize: null, style: null, fill: null, ct);
        await _bot.SendMessage(chatId, $"Pen thickness set to {value}.", cancellationToken: ct);
    }

    /// <summary>Set the vertex marker radius: <c>/markers &lt;n&gt;</c> (0 hides them).</summary>
    private async Task PenMarkersAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        if (!TryParseNonNegative(cmd.Args, out double value))
        {
            await _bot.SendMessage(chatId, "Usage: /markers <n> (radius in pixels; 0 hides markers), e.g. /markers 6", cancellationToken: ct);
            return;
        }
        await _editing.ConfigurePenAsync(userId, color: null, thickness: null, markerSize: value, style: null, fill: null, ct);
        await _bot.SendMessage(chatId, $"Marker size set to {value}.", cancellationToken: ct);
    }

    /// <summary>Parse a non-negative invariant-culture number from the first argument.</summary>
    private static bool TryParseNonNegative(IReadOnlyList<string> args, out double value)
    {
        value = 0;
        return args.Count > 0
            && double.TryParse(args[0], NumberStyles.Float, CultureInfo.InvariantCulture, out value)
            && value >= 0;
    }

    /// <summary>Set the line style: <c>/style &lt;solid|dashed|dotted&gt;</c>.</summary>
    private async Task PenStyleAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        string style = cmd.Args.Count == 0 ? "" : cmd.Args[0].ToLowerInvariant();
        if (style is not ("solid" or "dashed" or "dotted"))
        {
            await _bot.SendMessage(chatId, "Usage: /style <solid|dashed|dotted>, e.g. /style dashed", cancellationToken: ct);
            return;
        }
        await _editing.ConfigurePenAsync(userId, color: null, thickness: null, markerSize: null, style: style, fill: null, ct);
        await _bot.SendMessage(chatId, $"Line style set to {style}.", cancellationToken: ct);
    }

    /// <summary>Set the closed-shape fill: <c>/fill &lt;#hex|name|none&gt;</c>.</summary>
    private async Task PenFillAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        if (cmd.Args.Count == 0)
        {
            await _bot.SendMessage(chatId, "Usage: /fill <#hex|name|none>, e.g. /fill #00ff00 (none clears it)", cancellationToken: ct);
            return;
        }
        string fill = cmd.Args[0];
        bool clear = fill.Equals("none", StringComparison.OrdinalIgnoreCase)
            || fill.Equals("clear", StringComparison.OrdinalIgnoreCase)
            || fill.Equals("transparent", StringComparison.OrdinalIgnoreCase);
        await _editing.ConfigurePenAsync(userId, color: null, thickness: null, markerSize: null, style: null, fill: clear ? "none" : fill, ct);
        await _bot.SendMessage(
            chatId,
            clear ? "Fill cleared (closed shapes are unfilled)." : $"Fill set to {fill} (applies to closed shapes).",
            cancellationToken: ct);
    }

    private async Task PenAsync(long userId, long chatId, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(userId, ct);
        await _bot.SendMessage(chatId, Replies.PenText(session.Edits.Pen), cancellationToken: ct);
    }

    /// <summary>Step back one edit (crop/rotate/filter/draw), then re-render.</summary>
    private async Task UndoAsync(long userId, long chatId, CancellationToken ct)
    {
        UserSession before = await _store.GetAsync(userId, ct);
        if (!before.HasImage)
        {
            await _bot.SendMessage(chatId, "No working image — upload a photo or use /blank first.", cancellationToken: ct);
            return;
        }
        if (before.EditHistory.Count == 0)
        {
            await _bot.SendMessage(chatId, "Nothing to undo.", replyMarkup: Keyboards.EditMenu(before.ActiveProjectId is not null), cancellationToken: ct);
            return;
        }
        await _editing.UndoAsync(userId, ct);
        await RenderAndSendAsync(userId, chatId, ct);
    }

    /// <summary>Re-apply the most recently undone edit, then re-render.</summary>
    private async Task RedoAsync(long userId, long chatId, CancellationToken ct)
    {
        UserSession before = await _store.GetAsync(userId, ct);
        if (!before.HasImage)
        {
            await _bot.SendMessage(chatId, "No working image — upload a photo or use /blank first.", cancellationToken: ct);
            return;
        }
        if (before.EditRedo.Count == 0)
        {
            await _bot.SendMessage(chatId, "Nothing to redo.", replyMarkup: Keyboards.EditMenu(before.ActiveProjectId is not null), cancellationToken: ct);
            return;
        }
        await _editing.RedoAsync(userId, ct);
        await RenderAndSendAsync(userId, chatId, ct);
    }

    /// <summary>Remove the most recently drawn line/shape, then re-render.</summary>
    private async Task UndoLineAsync(long userId, long chatId, CancellationToken ct)
    {
        UserSession session = await _editing.RemoveLastLineAsync(userId, ct);
        if (!session.HasImage)
        {
            await _bot.SendMessage(chatId, "No working image — upload a photo or use /blank first.", cancellationToken: ct);
            return;
        }
        await RenderAndSendAsync(userId, chatId, ct);
    }

    /// <summary>Remove every drawn line/shape, then re-render.</summary>
    private async Task ClearLinesAsync(long userId, long chatId, CancellationToken ct)
    {
        UserSession session = await _editing.ClearLinesAsync(userId, ct);
        if (!session.HasImage)
        {
            await _bot.SendMessage(chatId, "No working image — upload a photo or use /blank first.", cancellationToken: ct);
            return;
        }
        await RenderAndSendAsync(userId, chatId, ct);
    }

    /// <summary>Rotate clockwise: <c>/rotate &lt;n&gt;</c> quarter-turns (bare lists the variants).</summary>
    private async Task RotateAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        if (cmd.Args.Count == 0 || !int.TryParse(cmd.Args[0], out int turns))
        {
            await _bot.SendMessage(chatId, Replies.RotateVariants(), cancellationToken: ct);
            return;
        }
        await _editing.RotateAsync(userId, turns, ct);
        await RenderAndSendAsync(userId, chatId, ct);
    }

    /// <summary>
    /// Set or clear the filter: <c>/filter &lt;bw|sepia|invert|contour|none|color&gt;</c>
    /// (bare lists the variants plus the filter submenu).
    /// </summary>
    private async Task FilterAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        if (cmd.ArgumentText.Length == 0)
        {
            await _bot.SendMessage(chatId, Replies.FilterVariants(), replyMarkup: Keyboards.FilterSubmenu(), cancellationToken: ct);
            return;
        }
        await _editing.SetFilterAsync(userId, cmd.ArgumentText, ct);
        await RenderAndSendAsync(userId, chatId, ct);
    }

    /// <summary>Clear pending edits but keep the working image.</summary>
    private async Task ResetAsync(long userId, long chatId, CancellationToken ct)
    {
        await _editing.ResetEditsAsync(userId, ct);
        await RenderAndSendAsync(userId, chatId, ct);
    }

    /// <summary>Drop the working image and active project entirely.</summary>
    private async Task DropAsync(long userId, long chatId, CancellationToken ct)
    {
        await _editing.DropImageAsync(userId, ct);
        await _bot.SendMessage(
            chatId,
            "Dropped the working image. Send a photo or use /blank to start again.",
            replyMarkup: Keyboards.MainMenu(),
            cancellationToken: ct);
    }

    /// <summary>Re-render and send the current result (non-mutating — never triggers auto-sync).</summary>
    private Task ImageAsync(long userId, long chatId, CancellationToken ct) =>
        RenderAndSendAsync(userId, chatId, ct, mutating: false);

    /// <summary>Toggle live sync for the active project: <c>/sync [on|off]</c>.</summary>
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

    /// <summary>Set the active project's accent colour: <c>/project-color &lt;#hex|name|clear&gt;</c>.</summary>
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

    /// <summary>
    /// Set the active project's expiry: <c>/expire &lt;amount&gt;</c> (e.g. <c>3 days</c>,
    /// <c>1 week</c>, <c>2 weeks</c>, <c>1 month</c>), <c>/expire never</c> to keep it forever, or
    /// bare <c>/expire</c> to show the current expiry and the duration picker. <c>/expire custom</c>
    /// (the Custom… button) arms a one-shot free-text prompt consumed by <see cref="UpdateRouter"/>.
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
    /// Remove the active project from its server: bare <c>/delete</c> (or the 🗑 Remove button)
    /// asks for confirmation; <c>/delete confirm</c> (the confirm button) actually deletes it, then
    /// clears the active project and turns live sync off. Destructive, so it never deletes on a
    /// single tap. The working image is kept so it can be re-saved elsewhere.
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
            $"🗑 Removed '{removed}' from the server.",
            replyMarkup: Keyboards.MainMenu(),
            cancellationToken: ct);
    }

    /// <summary>Export and send the layout JSON as a document.</summary>
    private async Task JsonAsync(long userId, long chatId, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(userId, ct);
        if (!session.HasImage)
        {
            await _bot.SendMessage(
                chatId,
                "No working image — upload a photo or use /blank first.",
                cancellationToken: ct);
            return;
        }
        string json = _editing.ExportLayoutJson(session);
        byte[] bytes = Encoding.UTF8.GetBytes(json);
        string fileName = $"{SafeLabel(session.ImageLabel)}.json";
        using MemoryStream stream = new(bytes);
        InputFileStream document = InputFile.FromStream(stream, fileName);
        await _bot.SendDocument(chatId, document, caption: "Layout JSON", cancellationToken: ct);
    }

    private async Task StatusAsync(long userId, long chatId, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(userId, ct);
        await _bot.SendMessage(
            chatId,
            Replies.StatusText(session),
            replyMarkup: Keyboards.StatusMenu(session.ActiveProjectId is not null),
            cancellationToken: ct);
    }

    /// <summary>A friendly no-op acknowledgement.</summary>
    private Task CancelAsync(long chatId, CancellationToken ct) =>
        _bot.SendMessage(chatId, "Okay, never mind. Send /help for the command list.", cancellationToken: ct);

    /// <summary>Unknown command — point at /help.</summary>
    private Task UnknownAsync(long chatId, CancellationToken ct) =>
        _bot.SendMessage(chatId, "Unknown command. Send /help for the list.", cancellationToken: ct);

    /// <summary>
    /// Replay the current edit state through the CLI and send the rendered result as a photo
    /// with the edit menu. Shared by the mutating commands and by <see cref="UpdateRouter"/>
    /// after a fresh upload or layout apply.
    /// </summary>
    public async Task RenderAndSendAsync(long userId, long chatId, CancellationToken ct, bool mutating = true)
    {
        await _bot.SendChatAction(chatId, ChatAction.UploadPhoto, cancellationToken: ct);
        RenderResult result = await _editing.RenderAsync(userId, ct);
        UserSession session = await _store.GetAsync(userId, ct);
        string caption = BuildCaption(session, result);
        await using (FileStream stream = File.OpenRead(result.Path))
        {
            InputFileStream photo = InputFile.FromStream(stream, "result.png");
            await _bot.SendPhoto(
                chatId,
                photo,
                caption: caption,
                replyMarkup: Keyboards.EditMenu(session.ActiveProjectId is not null),
                cancellationToken: ct);
        }
        // Live sync: a mutating edit on a synced active project auto-uploads so peers see it.
        if (mutating && session.SyncEnabled && session.ActiveProjectId is not null)
        {
            await AutoSyncAsync(userId, chatId, ct);
        }
    }

    /// <summary>Push the current result to the active project (best-effort), surfacing a conflict.</summary>
    private async Task AutoSyncAsync(long userId, long chatId, CancellationToken ct)
    {
        try
        {
            ProjectRecord record = await _servers.SaveActiveProjectAsync(userId, ct);
            await _bot.SendMessage(chatId, $"↑ synced to '{record.Name}' (v{record.Version}).", cancellationToken: ct);
        }
        catch (ServerException ex)
        {
            await _bot.SendMessage(chatId, $"Couldn't sync: {ex.Message}", cancellationToken: ct);
        }
    }

    /// <summary>A short caption: label, rendered size, and the pending edits.</summary>
    private static string BuildCaption(UserSession session, RenderResult result)
    {
        string label = session.ImageLabel ?? "image";
        string edits = Replies.DescribeEdits(session.Edits);
        return $"{label} — {result.Size}\nEdits: {edits}";
    }

    /// <summary>A short human label for a URL source: its file name, else its host.</summary>
    private static string LabelFromUrl(string url)
    {
        if (Uri.TryCreate(url, UriKind.Absolute, out Uri? uri))
        {
            string name = Path.GetFileName(uri.LocalPath);
            return name.Length > 0 ? name : uri.Host;
        }
        return "image";
    }

    /// <summary>A filesystem-safe stem for the JSON download (defaults to "layout").</summary>
    private static string SafeLabel(string? label)
    {
        if (string.IsNullOrWhiteSpace(label))
        {
            return "layout";
        }
        StringBuilder sb = new();
        foreach (char c in label)
        {
            sb.Append(char.IsLetterOrDigit(c) || c is '-' or '_' ? c : '_');
        }
        string cleaned = sb.ToString().Trim('_');
        return cleaned.Length == 0 ? "layout" : cleaned;
    }
}
