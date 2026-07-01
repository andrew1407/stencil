using TgCommand = Telegram.Bot.Types.BotCommand;

namespace Stencil.TelegramBot.Bot.Telegram;

/// <summary>
/// The bot's slash-command menu, registered with Telegram on startup (<c>SetMyCommands</c>) so
/// the "/" autocomplete list always matches the code — no manual BotFather upkeep. Command names
/// are lowercase and hyphen-free per Telegram's rules (e.g. <c>projectcolor</c>, not
/// <c>project-color</c>), matching the aliases <see cref="CommandHandlers"/> dispatches.
/// </summary>
public static class BotCommandList
{
    public static IReadOnlyList<TgCommand> All() =>
    [
        new("help", "Show the commands and menu"),
        new("blank", "Start a blank canvas: [w h] [color]"),
        new("url", "Load an image from a link"),
        new("frame", "Grab a video frame: [n]"),
        new("crop", "Crop, e.g. x1=10% x2=90% y1=10% y2=90%"),
        new("rotate", "Rotate 90° × n (default 1)"),
        new("filter", "bw | sepia | none | color"),
        new("draw", "Draw line|rect|poly x1,y1 x2,y2 …"),
        new("color", "Set the pen colour"),
        new("thickness", "Set the pen stroke width"),
        new("markers", "Set vertex marker size (0 hides)"),
        new("style", "Line style: solid | dashed | dotted"),
        new("fill", "Closed-shape fill (or none)"),
        new("pen", "Show the current pen"),
        new("undo", "Step back one edit"),
        new("redo", "Step forward one edit"),
        new("undoline", "Remove the last drawn line"),
        new("clearlines", "Remove all drawn lines"),
        new("image", "Re-render and resend the result"),
        new("json", "Download the layout JSON"),
        new("reset", "Clear pending edits"),
        new("drop", "Discard the working image"),
        new("status", "Show image, edits and connections"),
        new("connect", "Connect a server: <url> [token]"),
        new("disconnect", "Forget a connection"),
        new("connections", "List connected servers"),
        new("projects", "List server projects"),
        new("fetch", "Open a project by name or id"),
        new("create", "Save the result as a new project"),
        new("save", "Save back to the active project"),
        new("sync", "Live sync on/off (auto-upload + pull)"),
        new("projectcolor", "Set the project accent colour"),
    ];
}
