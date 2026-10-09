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
    // Capped: a flood of unknown ids must not grow the map without bound, nor spend the bot's send rate.
    public const int MaxRefusalsTracked = 1024;
    public const int MaxRefusalsPerWindow = 100;

    private readonly IBotPolicy _options;
    private readonly ITelegramBotClient _bot;
    private readonly ILogger _logger;
    private readonly TimeProvider _clock;
    private readonly Dictionary<long, DateTimeOffset> _lastReply = new();
    private DateTimeOffset _windowStart = DateTimeOffset.MinValue;
    private int _repliesInWindow;

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

    // Asked on the polling loop, so the refusal is sent without awaiting it.
    public bool Allows(long userId, long chatId, CancellationToken ct)
    {
        if (_options.AllowedFor(userId))
        {
            return true;
        }
        (bool due, bool reply) = refusal(userId);
        if (due)
        {
            _logger.LogWarning(
                "Refused request from Telegram user {UserId}; add the id to "
                + "STENCIL_BOT_ALLOWED_USERS to allow it", userId);
        }
        if (reply)
        {
            _ = sendRefusalAsync(chatId, ct);
        }
        return false;
    }

    private async Task sendRefusalAsync(long chatId, CancellationToken ct)
    {
        try
        {
            await _bot.SendMessage(chatId, Replies.AccessRefused(_options.AllowedUsers.Count == 0), cancellationToken: ct);
        }
        catch (Exception ex)
        {
            if (ex is not OperationCanceledException)
            {
                _logger.LogWarning(ex, "Failed to send the refusal to chat {ChatId}", chatId);
            }
        }
    }

    // Due once per id per window; replied only while the window's global budget lasts.
    private (bool Due, bool Reply) refusal(long userId)
    {
        DateTimeOffset now = _clock.GetUtcNow();
        TimeSpan window = _options.RefusalReplyWindow;
        lock (_lastReply)
        {
            if (_lastReply.TryGetValue(userId, out DateTimeOffset last) && now - last < window)
            {
                return (false, false);
            }
            if (_lastReply.Count >= MaxRefusalsTracked && !_lastReply.ContainsKey(userId))
            {
                evict(now, window);
            }
            _lastReply[userId] = now;
            if (now - _windowStart >= window)
            {
                _windowStart = now;
                _repliesInWindow = 0;
            }
            if (_repliesInWindow >= MaxRefusalsPerWindow)
            {
                return (true, false);
            }
            _repliesInWindow++;
            return (true, true);
        }
    }

    // Drops every expired id; when none has expired, the oldest goes, so the rest keep their throttle.
    private void evict(DateTimeOffset now, TimeSpan window)
    {
        List<long> expired = _lastReply.Where(kv => now - kv.Value >= window).Select(kv => kv.Key).ToList();
        if (expired.Count == 0)
        {
            expired.Add(_lastReply.MinBy(kv => kv.Value).Key);
        }
        foreach (long id in expired)
        {
            _lastReply.Remove(id);
        }
    }
}
