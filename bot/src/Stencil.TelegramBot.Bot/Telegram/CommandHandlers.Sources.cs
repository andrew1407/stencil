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
using Stencil.TelegramBot.Infrastructure.Links;
using Telegram.Bot;
using Telegram.Bot.Types;
using Telegram.Bot.Types.Enums;
using Telegram.Bot.Types.ReplyMarkups;

namespace Stencil.TelegramBot.Bot.Telegram;

// CommandHandlers — image-source commands: /url, /sourcesite, /sourceupload and their
// argument parsers. Class doc lives in CommandHandlers.cs.
public sealed partial class CommandHandlers
{
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

    /// <summary>
    /// Scrape a web page's media into the chat: <c>/sourcesite &lt;url&gt; [count] [filters…]</c>.
    /// Each image comes back as a photo, each video as a document, plus a one-line summary.
    /// The URL is SSRF-vetted like /url.
    /// </summary>
    private async Task SourceSiteAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        if (cmd.Args.Count == 0)
        {
            await _bot.SendMessage(chatId, SourceSiteUsage, cancellationToken: ct);
            return;
        }
        string url = cmd.Args[0];
        if (!TryParseScrapeArgs(url, cmd.Args, out ScrapeRequest request, out string? error))
        {
            await _bot.SendMessage(chatId, $"{error}\n\n{SourceSiteUsage}", cancellationToken: ct);
            return;
        }
        // Same trust boundary as /url: the bot is open to any Telegram user, so reject
        // loopback/private/metadata hosts before the CLI fetches the page.
        await RemoteImageUrl.ValidateAsync(url, ct);
        string host = Uri.TryCreate(url, UriKind.Absolute, out Uri? uri) ? uri.Host : url;
        // Fetching the page and downloading its media can take a while, so post the spinning
        // notice right away instead of leaving the chat silent — it clears itself once the
        // results land, whether the scrape succeeded or threw.
        ProgressNotice progress = await ProgressNotice.StartAsync(
            _bot, chatId, $"Scraping {host}…", ChatAction.UploadPhoto, ct);
        ScrapeResult result;
        try
        {
            result = await _editing.ScrapeAsync(userId, request, ct);
        }
        finally
        {
            await progress.StopAsync();
        }
        foreach (ScrapedFile file in result.Files)
        {
            string name = Path.GetFileName(file.Path);
            await using FileStream stream = File.OpenRead(file.Path);
            if (file.Width is int w && file.Height is int h)
            {
                InputFileStream photo = InputFile.FromStream(stream, name);
                await _bot.SendPhoto(chatId, photo, caption: $"{name} — {w}x{h}", cancellationToken: ct);
            }
            else
            {
                InputFileStream document = InputFile.FromStream(stream, name);
                await _bot.SendDocument(chatId, document, caption: name, cancellationToken: ct);
            }
        }
        await _bot.SendMessage(
            chatId,
            Replies.Tag(Replies.Tone.Success, $"Scraped {result.Files.Count} file(s) from {host}."),
            cancellationToken: ct);
    }

    private const string SourceSiteUsage =
        "Usage: /sourcesite <http(s) link> [count (default 5, 0 = all)] "
        + "[filter=img|video|background|poster] "
        + "[format=png|jpg|…] [name=<regex>] [minw=…] [maxw=…] [minh=…] [maxh=…] [group=N]\n"
        + "name= is a case-insensitive regex matched on each media URL.\n"
        + "e.g. /sourcesite https://example.com 6 filter=img format=png|jpg name=cat minw=200";

    /// <summary>
    /// Parse the /sourcesite operands: a bare integer is the item count, everything else is a
    /// <c>key=value</c> option. A malformed value yields false with a readable <paramref name="error"/>.
    /// </summary>
    private static bool TryParseScrapeArgs(string url, IReadOnlyList<string> args, out ScrapeRequest request, out string? error)
    {
        error = null;
        // A minimal fallback the callers ignore on `false`; the success path overwrites it below.
        request = new ScrapeRequest { Url = url };
        int? count = null, group = null, minW = null, maxW = null, minH = null, maxH = null;
        string? filter = null, format = null, name = null;
        for (int i = 1; i < args.Count; i++)
        {
            string token = args[i];
            int eq = token.IndexOf('=');
            if (eq < 0)
            {
                // A bare integer is the item count (e.g. "/sourcesite <url> 6").
                if (int.TryParse(token, out int bare) && bare >= 0)
                {
                    count = bare;
                    continue;
                }
                error = $"Unrecognised option '{token}'.";
                return false;
            }
            string key = token[..eq].ToLowerInvariant();
            string value = token[(eq + 1)..];
            switch (key)
            {
                case "count": if (!SetInt(ref count, value, key, out error)) return false; break;
                case "group": if (!SetInt(ref group, value, key, out error)) return false; break;
                case "minw" or "minwidth": if (!SetInt(ref minW, value, key, out error)) return false; break;
                case "maxw" or "maxwidth": if (!SetInt(ref maxW, value, key, out error)) return false; break;
                case "minh" or "minheight": if (!SetInt(ref minH, value, key, out error)) return false; break;
                case "maxh" or "maxheight": if (!SetInt(ref maxH, value, key, out error)) return false; break;
                case "filter": filter = value; break;
                case "format": format = value; break;
                // A regex matched against each media URL (passed through as --source-name).
                case "name": name = value; break;
                default:
                    error = $"Unrecognised option '{key}'.";
                    return false;
            }
        }
        request = new ScrapeRequest
        {
            Url = url,
            // A batch scrape with no explicit count defaults to 5 (a sensible chat-sized page);
            // an explicit 0 means "all" and rides through as `--source-count 0`, which the CLI
            // interprets as every match.
            Count = count ?? 5,
            Group = group,
            Filter = filter,
            Format = format,
            Name = name,
            MinWidth = minW,
            MaxWidth = maxW,
            MinHeight = minH,
            MaxHeight = maxH,
        };
        return true;
    }

    private static bool SetInt(ref int? target, string value, string key, out string? error)
    {
        if (int.TryParse(value, out int parsed) && parsed >= 0)
        {
            target = parsed;
            error = null;
            return true;
        }
        error = $"'{key}' needs a non-negative number (got '{value}').";
        return false;
    }

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
