using System.Collections.Concurrent;
using Microsoft.Extensions.Logging;
using Stencil.TelegramBot.Domain.Configuration;
using Telegram.Bot;

namespace Stencil.TelegramBot.Bot.Telegram;

// The global, fail-closed allowlist (STENCIL_BOT_ALLOWED_USERS): an unlisted user gets only /start
// and /help, and an empty list turns the bot off for everyone. The operator detail goes to the log,
// once per id.
public sealed class AccessGate
{
    private readonly IBotPolicy _options;
    private readonly ITelegramBotClient _bot;
    private readonly ILogger _logger;
    // Capped: a flood of unknown ids must not grow it without bound.
    private const int _maxRefusalsLogged = 1024;
    private readonly ConcurrentDictionary<long, byte> _refusalsLogged = new();

    public AccessGate(IBotPolicy options, ITelegramBotClient bot, ILogger logger)
    {
        _options = options;
        _bot = bot;
        _logger = logger;
    }

    // /start only when bare: with a payload it is a deep link that connects out and fetches a
    // project.
    public static bool IsUngated(BotCommand command) =>
        command.Verb == "help" || (command.Verb == "start" && command.ArgumentText.Length == 0);

    public async Task<bool> AllowsAsync(long userId, long chatId, CancellationToken ct)
    {
        if (_options.AllowedFor(userId))
        {
            return true;
        }
        if (_refusalsLogged.Count >= _maxRefusalsLogged)
        {
            _refusalsLogged.Clear();
        }
        if (_refusalsLogged.TryAdd(userId, 0))
        {
            _logger.LogWarning(
                "Refused request from Telegram user {UserId}; add the id to "
                + "STENCIL_BOT_ALLOWED_USERS to allow it", userId);
        }
        await _bot.SendMessage(
            chatId,
            Replies.Tag(Replies.Tone.ERROR, _options.AllowedUsers.Count == 0
                ? "This bot isn't accepting requests."
                : "This bot isn't enabled for your account."),
            cancellationToken: ct);
        return false;
    }
}
