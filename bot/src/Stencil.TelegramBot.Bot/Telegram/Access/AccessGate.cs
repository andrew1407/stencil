using System.Collections.Concurrent;
using Microsoft.Extensions.Logging;
using Stencil.TelegramBot.Domain.Configuration;
using Telegram.Bot;
using Stencil.TelegramBot.Bot.Telegram.Commands;
using Stencil.TelegramBot.Bot.Telegram.Messaging;

namespace Stencil.TelegramBot.Bot.Telegram.Access;

// The global, fail-closed allowlist (STENCIL_BOT_ALLOWED_USERS): an unlisted user gets only /start and
// /help, and an empty list turns the bot off for everyone.
public sealed class AccessGate
{
    private readonly IBotPolicy _options;
    private readonly ITelegramBotClient _bot;
    private readonly ILogger _logger;
    private readonly TimeProvider _clock;
    // Capped: a flood of unknown ids must not grow either without bound.
    private const int _maxRefusalsTracked = 1024;
    private readonly ConcurrentDictionary<long, byte> _refusalsLogged = new();
    private readonly Dictionary<long, DateTimeOffset> _lastReply = new();

    public AccessGate(IBotPolicy options, ITelegramBotClient bot, ILogger logger, TimeProvider? clock = null)
    {
        _options = options;
        _bot = bot;
        _logger = logger;
        _clock = clock ?? TimeProvider.System;
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
        if (_refusalsLogged.Count >= _maxRefusalsTracked)
        {
            _refusalsLogged.Clear();
        }
        if (_refusalsLogged.TryAdd(userId, 0))
        {
            _logger.LogWarning(
                "Refused request from Telegram user {UserId}; add the id to "
                + "STENCIL_BOT_ALLOWED_USERS to allow it", userId);
        }
        if (!dueReply(userId))
        {
            return false;
        }
        // Runs on the polling loop too, so a failed send is logged, never thrown.
        try
        {
            await _bot.SendMessage(chatId, Replies.AccessRefused(_options.AllowedUsers.Count == 0), cancellationToken: ct);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            _logger.LogWarning(ex, "Failed to send the refusal to chat {ChatId}", chatId);
        }
        return false;
    }

    private bool dueReply(long userId)
    {
        DateTimeOffset now = _clock.GetUtcNow();
        lock (_lastReply)
        {
            if (_lastReply.TryGetValue(userId, out DateTimeOffset last) && now - last < _options.RefusalReplyWindow)
            {
                return false;
            }
            if (_lastReply.Count >= _maxRefusalsTracked)
            {
                _lastReply.Clear();
            }
            _lastReply[userId] = now;
            return true;
        }
    }
}
