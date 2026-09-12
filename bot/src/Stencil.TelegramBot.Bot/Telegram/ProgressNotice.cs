using Telegram.Bot;
using Telegram.Bot.Types;
using Telegram.Bot.Types.Enums;
using Telegram.Bot.Types.ReplyMarkups;

namespace Stencil.TelegramBot.Bot.Telegram;

// One message whose leading glyph spins every Tick, with the chat action re-armed on the same beat
// (Telegram's own fades after ~5 s); StopAsync deletes it and is idempotent. Every Telegram call
// here is best-effort and swallowed: the notice is cosmetic and must never fail the turn it
// decorates.
public sealed class ProgressNotice
{
    public static readonly string[] Frames = ["◐", "◓", "◑", "◒"];

    public static readonly TimeSpan Tick = TimeSpan.FromSeconds(3);

    private readonly ITelegramBotClient _bot;
    private readonly long _chatId;
    private readonly int? _messageId; // null = the send failed; spinner inert, chat action still runs
    private readonly string _text;
    private readonly ChatAction _action;
    // editMessageText DROPS an inline keyboard it is not given, so a Stop button has to be restated
    // every frame.
    private readonly InlineKeyboardMarkup? _markup;
    private readonly CancellationTokenSource _stop = new();
    private readonly Task _loop;
    private int _stopped;

    private ProgressNotice(
        ITelegramBotClient bot, long chatId, int? messageId, string text, ChatAction action,
        InlineKeyboardMarkup? markup)
    {
        _bot = bot;
        _chatId = chatId;
        _messageId = messageId;
        _text = text;
        _action = action;
        _markup = markup;
        _loop = Task.Run(RunAsync, CancellationToken.None);
    }

    public static string Frame(int i, string text) => $"{Frames[i % Frames.Length]} {text}";

    // Never throws: a failed send just yields a notice that keeps the chat action alive.
    public static async Task<ProgressNotice> StartAsync(
        ITelegramBotClient bot, long chatId, string text, ChatAction action, CancellationToken ct,
        InlineKeyboardMarkup? markup = null)
    {
        Message? sent = null;
        try
        {
            sent = await bot.SendMessage(chatId, Frame(0, text), replyMarkup: markup, cancellationToken: ct);
        }
        catch (Exception)
        {
        }
        return new ProgressNotice(bot, chatId, sent?.MessageId, text, action, markup);
    }

    public async Task StopAsync()
    {
        if (Interlocked.Exchange(ref _stopped, 1) != 0)
        {
            return;
        }
        await _stop.CancelAsync();
        try { await _loop; } catch (Exception) { /* the loop swallows its own */ }
        if (_messageId is int id)
        {
            // Not the turn's token: a cancelled turn still has to clear its notice.
            try { await _bot.DeleteMessage(_chatId, id, cancellationToken: CancellationToken.None); }
            catch (Exception) { /* a delete that races or ages out is harmless */ }
        }
        _stop.Dispose();
    }

    private async Task RunAsync()
    {
        for (int frame = 1; ; frame++)
        {
            try { await Task.Delay(Tick, _stop.Token); }
            catch (OperationCanceledException) { return; }
            try
            {
                await _bot.SendChatAction(_chatId, _action, cancellationToken: _stop.Token);
                if (_messageId is int id)
                {
                    await _bot.EditMessageText(
                        _chatId, id, Frame(frame, _text), replyMarkup: _markup, cancellationToken: _stop.Token);
                }
            }
            catch (Exception)
            {
            }
        }
    }
}
