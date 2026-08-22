using Telegram.Bot;
using Telegram.Bot.Types;
using Telegram.Bot.Types.Enums;
using Telegram.Bot.Types.ReplyMarkups;

namespace Stencil.TelegramBot.Bot.Telegram;

/// <summary>
/// The "still working" notice a slow turn posts the moment it starts: one message whose
/// leading glyph spins through <see cref="Frames"/> every <see cref="Tick"/>, with the chat
/// action re-armed on the same beat (Telegram's own fades after ~5 s). <see cref="StopAsync"/>
/// deletes it, so the finished turn leaves only its real reply behind.
/// </summary>
/// <remarks>
/// Every Telegram call here is best-effort and swallowed: the notice is cosmetic, so a hiccup
/// — or a test double that hands back no <see cref="Message"/> — must never fail the turn it
/// decorates. With no message to edit the spinner is inert and only the chat action rides on.
/// <see cref="StopAsync"/> is idempotent, so a catch path may stop it before the finally does.
/// </remarks>
public sealed class ProgressNotice
{
    /// <summary>The spinner frames, in order — a filled quarter circling clockwise.</summary>
    public static readonly string[] Frames = ["◐", "◓", "◑", "◒"];

    /// <summary>How often the frame advances and the chat action is re-armed.</summary>
    public static readonly TimeSpan Tick = TimeSpan.FromSeconds(3);

    private readonly ITelegramBotClient _bot;
    private readonly long _chatId;
    private readonly int? _messageId; // null = the send failed; spinner inert, chat action still runs
    private readonly string _text;
    private readonly ChatAction _action;
    // Re-sent with every frame: editMessageText DROPS an inline keyboard it is not given, so a
    // notice that carries a Stop button has to restate it or the button vanishes on the first tick.
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

    /// <summary>The notice text at frame <paramref name="i"/> — the spinner, then the message.</summary>
    public static string Frame(int i, string text) => $"{Frames[i % Frames.Length]} {text}";

    /// <summary>
    /// Post the notice and start spinning. Never throws: a failed send just yields a notice
    /// that keeps the chat action alive.
    /// </summary>
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
            // Cosmetic — the turn goes on without it.
        }
        return new ProgressNotice(bot, chatId, sent?.MessageId, text, action, markup);
    }

    /// <summary>Stop spinning and remove the notice. Idempotent; never throws.</summary>
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
                // One dropped frame changes nothing — keep spinning until the turn settles.
            }
        }
    }
}
