using Stencil.TelegramBot.Bot.Telegram;
using Telegram.Bot.Types;
using Telegram.Bot.Types.Enums;

namespace Stencil.TelegramBot.Bot;

// The polling loop's side of the pump: the allowlist admits an update before it takes a pump slot,
// so a stranger's flood never holds the room a listed user's update needs.
public sealed class UpdateIntake
{
    private readonly UpdatePump _pump;
    private readonly UpdateRouter _router;
    private readonly CancellationToken _shutdown;

    public UpdateIntake(UpdatePump pump, UpdateRouter router, CancellationToken shutdown)
    {
        _pump = pump;
        _router = router;
        _shutdown = shutdown;
        router.UseLanes((lane, work) => _pump.EnqueueAsync(lane, work));
    }

    // An edited message is not a new request: re-running an old /delete would delete again.
    public async Task OnMessageAsync(Message message, UpdateType type = UpdateType.Message)
    {
        if (type == UpdateType.Message && await _router.AdmitAsync(message, _shutdown))
        {
            await _pump.EnqueueAsync(UpdateRouter.LaneOf(message), () => _router.HandleMessageAsync(message, _shutdown));
        }
    }

    public async Task OnUpdateAsync(Update update)
    {
        if (update.CallbackQuery is CallbackQuery query && await _router.AdmitAsync(query, _shutdown))
        {
            await _pump.EnqueueAsync(UpdateRouter.LaneOf(query), () => _router.HandleUpdateAsync(update, _shutdown));
        }
    }
}
