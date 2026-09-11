using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Sessions;
using Telegram.Bot;
using Telegram.Bot.Types;
using Telegram.Bot.Types.Enums;

namespace Stencil.TelegramBot.Bot.Telegram;

// CommandHandlers — /url and /sourcesite: adopting a remote still, and scraping a page into a
// batch of them. Class doc lives in CommandHandlers.cs.
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
}
