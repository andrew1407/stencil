using System.Globalization;
using System.Text;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Sessions;
using Telegram.Bot;
using Stencil.TelegramBot.Bot.Telegram.Messaging;

namespace Stencil.TelegramBot.Bot.Telegram.Commands;

public sealed partial class CommandHandlers
{
    // The command-line sibling of uploading a .json document; URLs are SSRF-vetted like /url.
    private async Task layoutAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
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
        // A leading `combine` keeps the lines already drawn (the editors' Combine choice).
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
            // Same guard as /url: reject loopback/private/metadata hosts before fetching.
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

    private async Task blankAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
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

    private async Task formatAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
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

    private async Task cropAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
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

    private async Task frameAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        int frame = 0;
        if (cmd.Args.Count >= 1 && int.TryParse(cmd.Args[0], out int n))
        {
            frame = n;
        }
        await _editing.ExtractFrameAsync(userId, frame, ct);
        await RenderAndSendAsync(userId, chatId, ct);
    }

    private async Task rotateAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        if (cmd.Args.Count == 0 || !int.TryParse(cmd.Args[0], out int turns))
        {
            await _bot.SendMessage(chatId, Replies.RotateVariants(), cancellationToken: ct);
            return;
        }
        await _editing.RotateAsync(userId, turns, ct);
        await RenderAndSendAsync(userId, chatId, ct);
    }

    private async Task filterAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        if (cmd.ArgumentText.Length == 0)
        {
            await _bot.SendMessage(chatId, Replies.FilterVariants(), replyMarkup: Keyboards.FilterSubmenu(), cancellationToken: ct);
            return;
        }
        await _editing.SetFilterAsync(userId, cmd.ArgumentText, ct);
        await RenderAndSendAsync(userId, chatId, ct);
    }
}
