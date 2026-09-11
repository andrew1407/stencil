using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Sessions;
using Telegram.Bot;
using Telegram.Bot.Types;
using Telegram.Bot.Types.Enums;

namespace Stencil.TelegramBot.Bot.Telegram;

// CommandHandlers — /sourceupload: scrape a page and adopt ONE of its stills as the working
// image. Class doc lives in CommandHandlers.cs.
public sealed partial class CommandHandlers
{
    /// <summary>
    /// Scrape a page and load ONE of its stills as the working image: <c>/sourceupload &lt;url&gt;
    /// [index=0] [filters…]</c> — the chat analog of the console <c>/source-upload</c>. Adopts the
    /// still like /url does, then renders with the edit menu. The URL is SSRF-vetted like /url.
    /// </summary>
    private async Task SourceUploadAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        if (cmd.Args.Count == 0)
        {
            await _bot.SendMessage(chatId, SourceUploadUsage, cancellationToken: ct);
            return;
        }
        string url = cmd.Args[0];
        if (!TryParseSourceUploadArgs(url, cmd.Args, out ScrapeRequest request, out int index, out string? error))
        {
            await _bot.SendMessage(chatId, $"{error}\n\n{SourceUploadUsage}", cancellationToken: ct);
            return;
        }
        // Same trust boundary as /url: the bot is open to any Telegram user, so reject
        // loopback/private/metadata hosts before the CLI fetches the page.
        await RemoteImageUrl.ValidateAsync(url, ct);
        string host = Uri.TryCreate(url, UriKind.Absolute, out Uri? uri) ? uri.Host : url;
        // The page fetch + download can take a moment; post the spinning notice and let it clear
        // itself once the still is in (mirrors /sourcesite).
        ProgressNotice progress = await ProgressNotice.StartAsync(
            _bot, chatId, $"Scraping {host}…", ChatAction.UploadPhoto, ct);
        ScrapeResult result;
        try
        {
            // Count = 1, Group = index isolates exactly the still at that 0-based index (the CLI's
            // paging window is filtered[index : index+1]); an empty result means no still lives there.
            result = await _editing.ScrapeAsync(userId, request, ct);
        }
        finally
        {
            await progress.StopAsync();
        }
        if (result.Files.Count == 0)
        {
            await _bot.SendMessage(chatId, $"No image at index {index}.\n\n{SourceUploadUsage}", cancellationToken: ct);
            return;
        }
        // Replace the working image via the local-file load path (mirrors how /url adopts a
        // source — Telegram has no modal, so there's no TTY-style confirmation).
        await _editing.SetImageFromLocalFileAsync(userId, result.Files[0].Path, LabelFromUrl(url), sourceUrl: url, ct: ct);
        await RenderAndSendAsync(userId, chatId, ct);
    }

    private const string SourceUploadUsage =
        "Usage: /sourceupload <http(s) link> [index=0] [format=png|jpg|…] [name=<regex>] "
        + "[minw=…] [maxw=…] [minh=…] [maxh=…]\n"
        + "Scrapes the page and loads its index-th still (img/background/poster — not video) as "
        + "the editable working image. name= is a case-insensitive regex on the media URL.\n"
        + "e.g. /sourceupload https://example.com 2 format=png name=cat minw=200";

    /// <summary>
    /// Parse the /sourceupload operands into a single-item <see cref="ScrapeRequest"/>: stills
    /// only (<c>Filter = "img|background|poster"</c>), <c>Count = 1</c>, <c>Group = index</c> so
    /// the CLI's paging window isolates one still. A malformed value yields false with an error.
    /// </summary>
    private static bool TryParseSourceUploadArgs(string url, IReadOnlyList<string> args, out ScrapeRequest request, out int index, out string? error)
    {
        error = null;
        index = 0;
        // A minimal fallback the callers ignore on `false`; the success path overwrites it below.
        request = new ScrapeRequest { Url = url };
        int? minW = null, maxW = null, minH = null, maxH = null;
        string? format = null, name = null;
        for (int i = 1; i < args.Count; i++)
        {
            string token = args[i];
            int eq = token.IndexOf('=');
            if (eq < 0)
            {
                // A bare integer is the 0-based item index (e.g. "/sourceupload <url> 2").
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
                    if (!SetInt(ref idx, value, key, out error)) return false;
                    index = idx!.Value;
                    break;
                case "minw" or "minwidth": if (!SetInt(ref minW, value, key, out error)) return false; break;
                case "maxw" or "maxwidth": if (!SetInt(ref maxW, value, key, out error)) return false; break;
                case "minh" or "minheight": if (!SetInt(ref minH, value, key, out error)) return false; break;
                case "maxh" or "maxheight": if (!SetInt(ref maxH, value, key, out error)) return false; break;
                case "format": format = value; break;
                // A regex matched against each media URL (narrows the candidate stills).
                case "name": name = value; break;
                default:
                    error = $"Unrecognised option '{key}'.";
                    return false;
            }
        }
        request = new ScrapeRequest
        {
            Url = url,
            // Image-category stills only (exclude video); the single item at `index` is isolated
            // by the CLI's paging window (Count = 1, Group = index).
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
