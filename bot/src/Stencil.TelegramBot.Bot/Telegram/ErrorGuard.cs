using Microsoft.Extensions.Logging;
using Stencil.TelegramBot.Domain.Exceptions;
using Telegram.Bot;

namespace Stencil.TelegramBot.Bot.Telegram;

// A domain error is surfaced to the chat verbatim, a deployment fault tells the chat a sentence and
// the operator the whole story, an unexpected one is logged and apologised for. Shutdown unwinds
// quietly.
public sealed class ErrorGuard
{
    private readonly ITelegramBotClient _bot;
    private readonly ILogger _logger;

    public ErrorGuard(ITelegramBotClient bot, ILogger logger)
    {
        _bot = bot;
        _logger = logger;
    }

    public async Task RunAsync(long chatId, Func<Task> action, CancellationToken ct)
    {
        try
        {
            await action();
        }
        catch (InvalidOperationException ex)
        {
            await replyError(chatId, ex.Message, ct);
        }
        catch (ServerException ex)
        {
            await replyError(chatId, ex.Message, ct);
        }
        catch (StencilCliException ex)
        {
            if (ex.OperatorDetail is string detail)
            {
                _logger.LogError("Stencil CLI unavailable: {Detail}", detail);
            }
            await replyError(chatId, ex.Message, ct);
        }
        catch (OperationCanceledException)
        {
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Unexpected error handling update for chat {ChatId}", chatId);
            await replyError(chatId, "Sorry, something went wrong handling that. Please try again.", ct);
        }
    }

    // Best-effort: a failed reply must not mask the original error.
    private async Task replyError(long chatId, string message, CancellationToken ct)
    {
        try
        {
            await _bot.SendMessage(chatId, Replies.Tag(Replies.Tone.Error, message), cancellationToken: ct);
        }
        catch (Exception ex)
        {
            _logger.LogWarning(ex, "Failed to send error reply to chat {ChatId}", chatId);
        }
    }
}
