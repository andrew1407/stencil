using System.Globalization;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Sessions;
using Telegram.Bot;
using Stencil.TelegramBot.Bot.Telegram.Messaging;

namespace Stencil.TelegramBot.Bot.Telegram.Commands;

public sealed partial class CommandHandlers
{
    private async Task drawAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        if (cmd.Args.Count == 0)
        {
            await _bot.SendMessage(chatId, Replies.DrawHelp(), cancellationToken: ct);
            return;
        }
        string shape = cmd.Args[0];
        IReadOnlyList<string> pointTokens = cmd.Args.Skip(1).ToList();
        await drawShapeAsync(userId, chatId, shape, pointTokens, ct);
    }

    private async Task drawShapeAsync(long userId, long chatId, string shape, IReadOnlyList<string> pointTokens, CancellationToken ct)
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

    private async Task penColorAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        if (cmd.Args.Count == 0)
        {
            await _bot.SendMessage(chatId, "Usage: /color <#hex|name>, e.g. /color #ff5623", cancellationToken: ct);
            return;
        }
        await _editing.ConfigurePenAsync(userId, color: cmd.Args[0], thickness: null, pointSize: null, style: null, fill: null, ct);
        await _bot.SendMessage(chatId, $"Pen colour set to {cmd.Args[0]}.", cancellationToken: ct);
    }

    private async Task penThicknessAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        if (!tryParseNonNegative(cmd.Args, out double value))
        {
            await _bot.SendMessage(chatId, "Usage: /thickness <n> (pixels, e.g. 4)", cancellationToken: ct);
            return;
        }
        await _editing.ConfigurePenAsync(userId, color: null, thickness: value, pointSize: null, style: null, fill: null, ct);
        await _bot.SendMessage(chatId, $"Pen thickness set to {value}.", cancellationToken: ct);
    }

    private async Task penPointsAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        if (!tryParseNonNegative(cmd.Args, out double value))
        {
            await _bot.SendMessage(chatId, "Usage: /points <n> (radius in pixels; 0 hides points), e.g. /points 6", cancellationToken: ct);
            return;
        }
        await _editing.ConfigurePenAsync(userId, color: null, thickness: null, pointSize: value, style: null, fill: null, ct);
        await _bot.SendMessage(chatId, $"Point size set to {value}.", cancellationToken: ct);
    }

    private static bool tryParseNonNegative(IReadOnlyList<string> args, out double value)
    {
        value = 0;
        return args.Count > 0
            && double.TryParse(args[0], NumberStyles.Float, CultureInfo.InvariantCulture, out value)
            && value >= 0;
    }

    private async Task penStyleAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
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

    private async Task penFillAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
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

    private async Task penAsync(long userId, long chatId, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(userId, ct);
        await _bot.SendMessage(chatId, Replies.PenText(session.Edits.Pen), cancellationToken: ct);
    }
}
