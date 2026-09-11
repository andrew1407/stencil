using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Sessions;
using Telegram.Bot;

namespace Stencil.TelegramBot.Bot.Telegram;

/// <summary>
/// A plain-text reply to an armed prompt (the custom-expiry entry, a project name/description).
/// One-shot: the flag is cleared before the matching command is dispatched.
/// </summary>
public sealed class PendingInputLink : IMessageHandler
{
    private readonly CommandHandlers _handlers;
    private readonly ISessionStore _store;

    public PendingInputLink(CommandHandlers handlers, ISessionStore store)
    {
        _handlers = handlers;
        _store = store;
    }

    public async Task<bool> TryHandleAsync(MessageContext ctx, CancellationToken ct)
    {
        if (ctx.Text is not string reply)
        {
            return false;
        }
        UserSession session = await _store.GetAsync(ctx.UserId, ct);
        string? pending = session.PendingInput;
        if (pending is not (PendingInputs.ExpiryDuration or PendingInputs.ProjectName or PendingInputs.ProjectDescription))
        {
            return false;
        }
        await _store.SaveAsync(session with { PendingInput = null }, ct);
        string spec = reply.Trim();
        string[] args = spec.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
        BotCommand command = pending switch
        {
            // The whole reply is the new name (names may contain spaces), so pass it verbatim.
            PendingInputs.ProjectName => new BotCommand("projectname", spec, args),
            // The whole reply is the description; a lone "-" is the clear convention (→ empty).
            PendingInputs.ProjectDescription => spec == "-"
                ? new BotCommand("projectdescription", "", [])
                : new BotCommand("projectdescription", spec, args),
            _ => new BotCommand("expire", spec, args),
        };
        await _handlers.DispatchAsync(ctx.UserId, ctx.ChatId, command, ct);
        return true;
    }
}

/// <summary>
/// A pasted http(s) link (no command, no attachment) is treated as <c>/url</c> — fetch it. Words
/// AROUND the link are a request about it, so in chat mode the link loads first and the rest goes
/// to the assistant. Without chat mode the link alone still wins.
/// </summary>
public sealed class UrlLink : IMessageHandler
{
    private readonly CommandHandlers _handlers;
    private readonly ISessionStore _store;
    private readonly ITelegramBotClient _bot;

    public UrlLink(CommandHandlers handlers, ISessionStore store, ITelegramBotClient bot)
    {
        _handlers = handlers;
        _store = store;
        _bot = bot;
    }

    public async Task<bool> TryHandleAsync(MessageContext ctx, CancellationToken ct)
    {
        if (ctx.Text is not string body || !TryExtractUrl(body, out string url))
        {
            return false;
        }
        await _handlers.DispatchAsync(ctx.UserId, ctx.ChatId, new BotCommand("url", url, [url]), ct);
        string around = body.Replace(url, " ", StringComparison.Ordinal).Trim();
        if (around.Length > 0 && (await _store.GetAsync(ctx.UserId, ct)).ChatMode)
        {
            // The /url render above already answered with the edit menu, which reads as
            // "done" — say the edit is still coming before the assistant turn runs (which
            // posts its own spinning notice for how long it takes).
            await _bot.SendMessage(ctx.ChatId, "✏️ Loaded — now editing per your request…", cancellationToken: ct);
            await _handlers.DispatchAsync(ctx.UserId, ctx.ChatId, CommandParser.Prompt(around), ct);
        }
        return true;
    }

    private static bool TryExtractUrl(string text, out string url)
    {
        url = "";
        foreach (string token in text.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries))
        {
            if (token.StartsWith("http://", StringComparison.OrdinalIgnoreCase)
                || token.StartsWith("https://", StringComparison.OrdinalIgnoreCase))
            {
                url = token;
                return true;
            }
        }
        return false;
    }
}

/// <summary>
/// Chat mode (<c>/chat</c>): anything left over — not a command, attachment, pending-prompt
/// answer or bare image link — is handed to the assistant exactly as "/prompt &lt;text&gt;" would be.
/// </summary>
public sealed class ChatModeLink : IMessageHandler
{
    private readonly CommandHandlers _handlers;
    private readonly ISessionStore _store;

    public ChatModeLink(CommandHandlers handlers, ISessionStore store)
    {
        _handlers = handlers;
        _store = store;
    }

    public async Task<bool> TryHandleAsync(MessageContext ctx, CancellationToken ct)
    {
        if (ctx.Text is not string chat || string.IsNullOrWhiteSpace(chat)
            || !(await _store.GetAsync(ctx.UserId, ct)).ChatMode)
        {
            return false;
        }
        await _handlers.DispatchAsync(ctx.UserId, ctx.ChatId, CommandParser.Prompt(chat), ct);
        return true;
    }
}

/// <summary>The chain's tail: any remaining non-blank text gets the "send a photo" hint.</summary>
public sealed class FallbackLink : IMessageHandler
{
    private readonly ITelegramBotClient _bot;

    public FallbackLink(ITelegramBotClient bot) => _bot = bot;

    public async Task<bool> TryHandleAsync(MessageContext ctx, CancellationToken ct)
    {
        if (!string.IsNullOrWhiteSpace(ctx.Text))
        {
            await _bot.SendMessage(
                ctx.ChatId, "Send a photo or an image link to edit, or /help for commands.", cancellationToken: ct);
        }
        return true;
    }
}
