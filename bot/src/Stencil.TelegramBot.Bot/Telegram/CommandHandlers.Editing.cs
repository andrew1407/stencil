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
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Links;
using Telegram.Bot;
using Telegram.Bot.Types;
using Telegram.Bot.Types.Enums;
using Telegram.Bot.Types.ReplyMarkups;

namespace Stencil.TelegramBot.Bot.Telegram;

// CommandHandlers — image-editing commands: layout/blank/format, crop/rotate/filter,
// draw + pen settings, undo/redo/reset/drop. Class doc lives in CommandHandlers.cs.
public sealed partial class CommandHandlers
{
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
                "Usage: /layout [combine] <layout JSON | link to a layout .json>, e.g. "
                + "/layout {\"imageWidth\":800,\"imageHeight\":600,\"lines\":[…]} — "
                + "or just upload the .json file. Add 'combine' first to keep the lines "
                + "already drawn and put the new ones on top (the default replaces them).",
                cancellationToken: ct);
            return;
        }
        UserSession session = await _store.GetAsync(userId, ct);
        if (!session.HasImage)
        {
            await _bot.SendMessage(chatId, "Upload an image (or use /blank) before applying a layout.", cancellationToken: ct);
            return;
        }
        // An optional leading `combine` keeps the lines already drawn (the editors' Combine
        // choice); without it the layout replaces them, as before.
        bool combine = cmd.Args.Count > 1 && cmd.Args[0].Equals("combine", StringComparison.OrdinalIgnoreCase);
        IReadOnlyList<string> args = combine ? [.. cmd.Args.Skip(1)] : cmd.Args;
        string argumentText = combine
            ? cmd.ArgumentText[cmd.ArgumentText.IndexOf(args[0], StringComparison.Ordinal)..]
            : cmd.ArgumentText;
        byte[] bytes;
        bool isUrl = args.Count == 1
            && Uri.TryCreate(args[0], UriKind.Absolute, out Uri? uri)
            && uri.Scheme is "http" or "https";
        if (isUrl)
        {
            // Same guard as /url: the bot is open to any Telegram user, so reject
            // loopback/private/metadata hosts before fetching.
            await RemoteImageUrl.ValidateAsync(args[0], ct);
            byte[]? fetched = await _layoutFetcher.FetchAsync(args[0], ct);
            if (fetched is null)
            {
                await _bot.SendMessage(chatId, "Could not fetch the layout from that link.", cancellationToken: ct);
                return;
            }
            bytes = fetched;
        }
        else
        {
            bytes = Encoding.UTF8.GetBytes(argumentText);
        }
        StencilLayout? layout = StencilLayoutParser.Parse(bytes);
        if (layout is null)
        {
            await _bot.SendMessage(chatId, "That isn't a valid Stencil layout JSON.", cancellationToken: ct);
            return;
        }
        await _editing.ApplyLayoutAsync(userId, layout, combine, ct);
        await RenderAndSendAsync(userId, chatId, ct);
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
        await _editing.ConfigurePenAsync(userId, color: cmd.Args[0], thickness: null, pointSize: null, style: null, fill: null, ct);
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
        await _editing.ConfigurePenAsync(userId, color: null, thickness: value, pointSize: null, style: null, fill: null, ct);
        await _bot.SendMessage(chatId, $"Pen thickness set to {value}.", cancellationToken: ct);
    }

    /// <summary>Set the vertex point radius: <c>/points &lt;n&gt;</c> (0 hides them).</summary>
    private async Task PenPointsAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        if (!TryParseNonNegative(cmd.Args, out double value))
        {
            await _bot.SendMessage(chatId, "Usage: /points <n> (radius in pixels; 0 hides points), e.g. /points 6", cancellationToken: ct);
            return;
        }
        await _editing.ConfigurePenAsync(userId, color: null, thickness: null, pointSize: value, style: null, fill: null, ct);
        await _bot.SendMessage(chatId, $"Point size set to {value}.", cancellationToken: ct);
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
        await _editing.ConfigurePenAsync(userId, color: null, thickness: null, pointSize: null, style: style, fill: null, ct);
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
        await _editing.ConfigurePenAsync(userId, color: null, thickness: null, pointSize: null, style: null, fill: clear ? "none" : fill, ct);
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

    /// <summary>
    /// Drop the working image and active project entirely — a full "start over", so the
    /// assistant's conversation (which talks about that image) is forgotten along with it.
    /// </summary>
    private async Task DropAsync(long userId, long chatId, CancellationToken ct)
    {
        await _editing.DropImageAsync(userId, ct);
        _prompts.ClearHistory(userId);
        await _bot.SendMessage(
            chatId,
            "Dropped the working image. Send a photo or use /blank to start again.",
            replyMarkup: Keyboards.MainMenu(),
            cancellationToken: ct);
    }

    /// <summary>Re-render and send the current result (non-mutating — never triggers auto-sync).</summary>
    private Task ImageAsync(long userId, long chatId, CancellationToken ct) =>
        RenderAndSendAsync(userId, chatId, ct, mutating: false);
}
