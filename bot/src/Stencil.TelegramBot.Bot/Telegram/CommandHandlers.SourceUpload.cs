using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Domain.Editing;
using Telegram.Bot;
using Telegram.Bot.Types.Enums;

namespace Stencil.TelegramBot.Bot.Telegram;

public sealed partial class CommandHandlers
{
    // The chat analog of the console /source-upload; adopts the still like /url does. SSRF-vetted
    // like /url.
    private async Task sourceUploadAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        if (cmd.Args.Count == 0)
        {
            await _bot.SendMessage(chatId, _sourceUploadUsage, cancellationToken: ct);
            return;
        }
        string url = cmd.Args[0];
        if (!tryParseSourceUploadArgs(url, cmd.Args, out ScrapeRequest request, out int index, out string? error))
        {
            await _bot.SendMessage(chatId, $"{error}\n\n{_sourceUploadUsage}", cancellationToken: ct);
            return;
        }
        // Same trust boundary as /url.
        await RemoteImageUrl.ValidateAsync(url, ct);
        string host = Uri.TryCreate(url, UriKind.Absolute, out Uri? uri) ? uri.Host : url;
        ProgressNotice progress = await ProgressNotice.StartAsync(
            _bot, chatId, $"Scraping {host}…", ChatAction.UploadPhoto, ct);
        ScrapeResult result;
        try
        {
            // Count = 1, Group = index isolates the still at that 0-based index (the CLI's paging
            // window).
            result = await _editing.ScrapeAsync(userId, request, ct);
        }
        finally
        {
            await progress.StopAsync();
        }
        if (result.Files.Count == 0)
        {
            await _bot.SendMessage(chatId, $"No image at index {index}.\n\n{_sourceUploadUsage}", cancellationToken: ct);
            return;
        }
        // Telegram has no modal, so there's no TTY-style confirmation before replacing the working
        // image.
        await _editing.SetImageFromLocalFileAsync(userId, result.Files[0].Path, labelFromUrl(url), sourceUrl: url, ct: ct);
        await RenderAndSendAsync(userId, chatId, ct);
    }

    private const string _sourceUploadUsage =
        "Usage: /sourceupload <http(s) link> [index=0] [format=png|jpg|…] [name=<regex>] "
        + "[minw=…] [maxw=…] [minh=…] [maxh=…]\n"
        + "Scrapes the page and loads its index-th still (img/background/poster — not video) as "
        + "the editable working image. name= is a case-insensitive regex on the media URL.\n"
        + "e.g. /sourceupload https://example.com 2 format=png name=cat minw=200";

    // Stills only, Count = 1, Group = index so the CLI's paging window isolates one still.
    private static bool tryParseSourceUploadArgs(string url, IReadOnlyList<string> args, out ScrapeRequest request, out int index, out string? error)
    {
        error = null;
        index = 0;
        request = new ScrapeRequest { Url = url };
        int? minW = null, maxW = null, minH = null, maxH = null;
        string? format = null, name = null;
        for (int i = 1; i < args.Count; i++)
        {
            string token = args[i];
            int eq = token.IndexOf('=');
            if (eq < 0)
            {
                if (int.TryParse(token, out int bare) && bare >= 0)
                {
                    index = bare;
                    continue;
                }
                error = $"Unrecognised option '{token}'.";
                return false;
            }
            string key = token[..eq].ToLowerInvariant();
            string value = token[(eq + 1)..];
            switch (key)
            {
                case "index":
                    int? idx = null;
                    if (!setInt(ref idx, value, key, out error)) return false;
                    index = idx!.Value;
                    break;
                case "minw" or "minwidth": if (!setInt(ref minW, value, key, out error)) return false; break;
                case "maxw" or "maxwidth": if (!setInt(ref maxW, value, key, out error)) return false; break;
                case "minh" or "minheight": if (!setInt(ref minH, value, key, out error)) return false; break;
                case "maxh" or "maxheight": if (!setInt(ref maxH, value, key, out error)) return false; break;
                case "format": format = value; break;
                case "name": name = value; break;
                default:
                    error = $"Unrecognised option '{key}'.";
                    return false;
            }
        }
        request = new ScrapeRequest
        {
            Url = url,
            Filter = "img|background|poster",
            Format = format,
            Name = name,
            Count = 1,
            Group = index,
            MinWidth = minW,
            MaxWidth = maxW,
            MinHeight = minH,
            MaxHeight = maxH,
        };
        return true;
    }
}
