namespace Stencil.TelegramBot.Bot.Telegram;

public sealed partial class CommandHandlers
{
    // Aliases live in botCommands.json (BotCommands.Canonical); a verb with no arm falls back to a
    // /help hint.
    private static readonly Dictionary<string, Route> Routes = new(StringComparer.Ordinal)
    {
        ["start"] = (h, u, c, cmd, ct) => h.startAsync(u, c, cmd, ct),
        ["help"] = (h, u, c, cmd, ct) => h.helpAsync(c, ct),
        ["prompt"] = (h, u, c, cmd, ct) => h.promptAsync(u, c, cmd, ct),
        ["chat"] = (h, u, c, cmd, ct) => h.chatAsync(u, c, cmd, ct),
        ["chatapi"] = (h, u, c, cmd, ct) => h.chatApiAsync(u, c, cmd, ct),
        ["connect"] = (h, u, c, cmd, ct) => h.connectAsync(u, c, cmd, ct),
        ["disconnect"] = (h, u, c, cmd, ct) => h.disconnectAsync(u, c, cmd, ct),
        ["connections"] = (h, u, c, cmd, ct) => h.connectionsAsync(u, c, cmd, ct),
        ["projects"] = (h, u, c, cmd, ct) => h.projectsAsync(u, c, cmd, ct),
        ["fetch"] = (h, u, c, cmd, ct) => h.fetchAsync(u, c, cmd, ct),
        ["create"] = (h, u, c, cmd, ct) => h.createAsync(u, c, cmd, ct),
        ["save"] = (h, u, c, cmd, ct) => h.saveAsync(u, c, ct),
        ["sync"] = (h, u, c, cmd, ct) => h.syncAsync(u, c, cmd, ct),
        ["link"] = (h, u, c, cmd, ct) => h.linkAsync(u, c, ct),
        ["projectcolor"] = (h, u, c, cmd, ct) => h.projectColorAsync(u, c, cmd, ct),
        ["projectname"] = (h, u, c, cmd, ct) => h.projectNameAsync(u, c, cmd, ct),
        ["projectdescription"] = (h, u, c, cmd, ct) => h.projectDescriptionAsync(u, c, cmd, ct),
        ["blankcolor"] = (h, u, c, cmd, ct) => h.blankColorAsync(u, c, cmd, ct),
        ["expire"] = (h, u, c, cmd, ct) => h.expireAsync(u, c, cmd, ct),
        ["delete"] = (h, u, c, cmd, ct) => h.deleteProjectAsync(u, c, cmd, ct),
        ["blank"] = (h, u, c, cmd, ct) => h.blankAsync(u, c, cmd, ct),
        ["format"] = (h, u, c, cmd, ct) => h.formatAsync(u, c, cmd, ct),
        ["url"] = (h, u, c, cmd, ct) => h.urlAsync(u, c, cmd, ct),
        ["sourcesite"] = (h, u, c, cmd, ct) => h.sourceSiteAsync(u, c, cmd, ct),
        ["sourceupload"] = (h, u, c, cmd, ct) => h.sourceUploadAsync(u, c, cmd, ct),
        ["frame"] = (h, u, c, cmd, ct) => h.frameAsync(u, c, cmd, ct),
        ["crop"] = (h, u, c, cmd, ct) => h.cropAsync(u, c, cmd, ct),
        ["rotate"] = (h, u, c, cmd, ct) => h.rotateAsync(u, c, cmd, ct),
        ["filter"] = (h, u, c, cmd, ct) => h.filterAsync(u, c, cmd, ct),
        ["draw"] = (h, u, c, cmd, ct) => h.drawAsync(u, c, cmd, ct),
        ["line"] = (h, u, c, cmd, ct) => h.drawShapeAsync(u, c, "line", cmd.Args, ct),
        ["rect"] = (h, u, c, cmd, ct) => h.drawShapeAsync(u, c, "rect", cmd.Args, ct),
        ["poly"] = (h, u, c, cmd, ct) => h.drawShapeAsync(u, c, "poly", cmd.Args, ct),
        ["color"] = (h, u, c, cmd, ct) => h.penColorAsync(u, c, cmd, ct),
        ["thickness"] = (h, u, c, cmd, ct) => h.penThicknessAsync(u, c, cmd, ct),
        ["points"] = (h, u, c, cmd, ct) => h.penPointsAsync(u, c, cmd, ct),
        ["style"] = (h, u, c, cmd, ct) => h.penStyleAsync(u, c, cmd, ct),
        ["fill"] = (h, u, c, cmd, ct) => h.penFillAsync(u, c, cmd, ct),
        ["pen"] = (h, u, c, cmd, ct) => h.penAsync(u, c, ct),
        ["undo"] = (h, u, c, cmd, ct) => h.undoAsync(u, c, ct),
        ["redo"] = (h, u, c, cmd, ct) => h.redoAsync(u, c, ct),
        ["undoline"] = (h, u, c, cmd, ct) => h.undoLineAsync(u, c, ct),
        ["clearlines"] = (h, u, c, cmd, ct) => h.clearLinesAsync(u, c, ct),
        ["reset"] = (h, u, c, cmd, ct) => h.resetAsync(u, c, ct),
        ["drop"] = (h, u, c, cmd, ct) => h.dropAsync(u, c, ct),
        ["image"] = (h, u, c, cmd, ct) => h.imageAsync(u, c, ct),
        ["layout"] = (h, u, c, cmd, ct) => h.layoutAsync(u, c, cmd, ct),
        ["json"] = (h, u, c, cmd, ct) => h.jsonAsync(u, c, ct),
        ["project"] = (h, u, c, cmd, ct) => h.projectAsync(u, c, ct),
        ["status"] = (h, u, c, cmd, ct) => h.statusAsync(u, c, ct),
        ["cancel"] = (h, u, c, cmd, ct) => h.cancelAsync(c, ct),
    };

    private delegate Task Route(CommandHandlers handlers, long userId, long chatId, BotCommand cmd, CancellationToken ct);

    // The drift test's view of the table.
    public static IReadOnlyCollection<string> HandledVerbs => Routes.Keys;

    public Task DispatchAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct) =>
        Routes.TryGetValue(BotCommands.Canonical(cmd.Verb), out Route? route)
            ? route(this, userId, chatId, cmd, ct)
            : unknownAsync(chatId, ct);
}
