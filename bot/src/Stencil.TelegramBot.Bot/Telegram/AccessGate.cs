using System.Collections.Concurrent;
using Microsoft.Extensions.Logging;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Telegram.Bot;

namespace Stencil.TelegramBot.Bot.Telegram;

/// <summary>
/// The bot's global, fail-closed allowlist (<c>STENCIL_BOT_ALLOWED_USERS</c>). Every command,
/// button tap and upload spends the operator's resources — a CLI process, disk in the data dir,
/// an outbound fetch to a user-named host, the single LLM API key — and anyone who finds the bot
/// can message it, so an unlisted user gets nothing but <c>/start</c> and <c>/help</c>. An empty
/// list therefore turns the bot off for everyone; that is the intended default.
/// </summary>
/// <remarks>
/// The refusal the user reads is one plain sentence. The env var, the procedure and the caller's
/// id are operator detail and go to the log instead, once per user id.
/// </remarks>
public sealed class AccessGate
{
    private readonly BotOptions _options;
    private readonly ITelegramBotClient _bot;
    private readonly ILogger _logger;
    // User ids whose refusal already carried the operator hint to the log.
    private readonly ConcurrentDictionary<long, byte> _refusalsLogged = new();

    public AccessGate(BotOptions options, ITelegramBotClient bot, ILogger logger)
    {
        _options = options;
        _bot = bot;
        _logger = logger;
    }

    /// <summary>
    /// The verbs an unlisted user may still run. <c>/start</c> only when it is the bare greeting:
    /// with a payload it is a deep link that connects out to a server and fetches a project, which
    /// is exactly the kind of work the gate exists to stop.
    /// </summary>
    public static bool IsUngated(BotCommand command) =>
        command.Verb == "help" || (command.Verb == "start" && command.ArgumentText.Length == 0);

    /// <summary>
    /// Whether <paramref name="userId"/> may act. When not, the chat gets the refusal and the
    /// operator hint goes to the log, and the caller does nothing else.
    /// </summary>
    public async Task<bool> AllowsAsync(long userId, long chatId, CancellationToken ct)
    {
        if (_options.AllowedFor(userId))
        {
            return true;
        }
        if (_refusalsLogged.TryAdd(userId, 0))
        {
            _logger.LogWarning(
                "Refused request from Telegram user {UserId}; add the id to "
                + "STENCIL_BOT_ALLOWED_USERS to allow it", userId);
        }
        await _bot.SendMessage(
            chatId,
            Replies.Tag(Replies.Tone.Error, _options.AllowedUsers.Count == 0
                ? "This bot isn't accepting requests."
                : "This bot isn't enabled for your account."),
            cancellationToken: ct);
        return false;
    }
}
