namespace Stencil.TelegramBot.Bot.Telegram;

public sealed partial class CommandHandlers
{
    /// <summary>
    /// The dispatch table: one canonical verb per handler. Aliases live in the
    /// <c>botCommands.json</c> asset (<see cref="BotCommands.Canonical"/>) alongside the "/" menu
    /// and /help text, so the vocabulary is written once; a verb with no arm falls back to a
    /// /help hint. ("/p" is normalised to "prompt" by <see cref="CommandParser"/>.)
    /// </summary>
    private static readonly Dictionary<string, Route> Routes = new(StringComparer.Ordinal)
    {
        ["start"] = (h, u, c, cmd, ct) => h.StartAsync(u, c, cmd, ct),
        ["help"] = (h, u, c, cmd, ct) => h.HelpAsync(c, ct),
        ["prompt"] = (h, u, c, cmd, ct) => h.PromptAsync(u, c, cmd, ct),
        ["chat"] = (h, u, c, cmd, ct) => h.ChatAsync(u, c, cmd, ct),
        ["chatapi"] = (h, u, c, cmd, ct) => h.ChatApiAsync(u, c, cmd, ct),
        ["connect"] = (h, u, c, cmd, ct) => h.ConnectAsync(u, c, cmd, ct),
        ["disconnect"] = (h, u, c, cmd, ct) => h.DisconnectAsync(u, c, cmd, ct),
        ["connections"] = (h, u, c, cmd, ct) => h.ConnectionsAsync(u, c, cmd, ct),
        ["projects"] = (h, u, c, cmd, ct) => h.ProjectsAsync(u, c, cmd, ct),
        ["fetch"] = (h, u, c, cmd, ct) => h.FetchAsync(u, c, cmd, ct),
        ["create"] = (h, u, c, cmd, ct) => h.CreateAsync(u, c, cmd, ct),
        ["save"] = (h, u, c, cmd, ct) => h.SaveAsync(u, c, ct),
        ["sync"] = (h, u, c, cmd, ct) => h.SyncAsync(u, c, cmd, ct),
        ["link"] = (h, u, c, cmd, ct) => h.LinkAsync(u, c, ct),
        ["projectcolor"] = (h, u, c, cmd, ct) => h.ProjectColorAsync(u, c, cmd, ct),
        ["projectname"] = (h, u, c, cmd, ct) => h.ProjectNameAsync(u, c, cmd, ct),
        ["projectdescription"] = (h, u, c, cmd, ct) => h.ProjectDescriptionAsync(u, c, cmd, ct),
        ["blankcolor"] = (h, u, c, cmd, ct) => h.BlankColorAsync(u, c, cmd, ct),
        ["expire"] = (h, u, c, cmd, ct) => h.ExpireAsync(u, c, cmd, ct),
        ["delete"] = (h, u, c, cmd, ct) => h.DeleteProjectAsync(u, c, cmd, ct),
        ["blank"] = (h, u, c, cmd, ct) => h.BlankAsync(u, c, cmd, ct),
        ["format"] = (h, u, c, cmd, ct) => h.FormatAsync(u, c, cmd, ct),
        ["url"] = (h, u, c, cmd, ct) => h.UrlAsync(u, c, cmd, ct),
        ["sourcesite"] = (h, u, c, cmd, ct) => h.SourceSiteAsync(u, c, cmd, ct),
        ["sourceupload"] = (h, u, c, cmd, ct) => h.SourceUploadAsync(u, c, cmd, ct),
        ["frame"] = (h, u, c, cmd, ct) => h.FrameAsync(u, c, cmd, ct),
        ["crop"] = (h, u, c, cmd, ct) => h.CropAsync(u, c, cmd, ct),
        ["rotate"] = (h, u, c, cmd, ct) => h.RotateAsync(u, c, cmd, ct),
        ["filter"] = (h, u, c, cmd, ct) => h.FilterAsync(u, c, cmd, ct),
        ["draw"] = (h, u, c, cmd, ct) => h.DrawAsync(u, c, cmd, ct),
        ["line"] = (h, u, c, cmd, ct) => h.DrawShapeAsync(u, c, "line", cmd.Args, ct),
        ["rect"] = (h, u, c, cmd, ct) => h.DrawShapeAsync(u, c, "rect", cmd.Args, ct),
        ["poly"] = (h, u, c, cmd, ct) => h.DrawShapeAsync(u, c, "poly", cmd.Args, ct),
        ["color"] = (h, u, c, cmd, ct) => h.PenColorAsync(u, c, cmd, ct),
        ["thickness"] = (h, u, c, cmd, ct) => h.PenThicknessAsync(u, c, cmd, ct),
        ["points"] = (h, u, c, cmd, ct) => h.PenPointsAsync(u, c, cmd, ct),
        ["style"] = (h, u, c, cmd, ct) => h.PenStyleAsync(u, c, cmd, ct),
        ["fill"] = (h, u, c, cmd, ct) => h.PenFillAsync(u, c, cmd, ct),
        ["pen"] = (h, u, c, cmd, ct) => h.PenAsync(u, c, ct),
        ["undo"] = (h, u, c, cmd, ct) => h.UndoAsync(u, c, ct),
        ["redo"] = (h, u, c, cmd, ct) => h.RedoAsync(u, c, ct),
        ["undoline"] = (h, u, c, cmd, ct) => h.UndoLineAsync(u, c, ct),
        ["clearlines"] = (h, u, c, cmd, ct) => h.ClearLinesAsync(u, c, ct),
        ["reset"] = (h, u, c, cmd, ct) => h.ResetAsync(u, c, ct),
        ["drop"] = (h, u, c, cmd, ct) => h.DropAsync(u, c, ct),
        ["image"] = (h, u, c, cmd, ct) => h.ImageAsync(u, c, ct),
        ["layout"] = (h, u, c, cmd, ct) => h.LayoutAsync(u, c, cmd, ct),
        ["json"] = (h, u, c, cmd, ct) => h.JsonAsync(u, c, ct),
        ["project"] = (h, u, c, cmd, ct) => h.ProjectAsync(u, c, ct),
        ["status"] = (h, u, c, cmd, ct) => h.StatusAsync(u, c, ct),
        ["cancel"] = (h, u, c, cmd, ct) => h.CancelAsync(c, ct),
    };

    /// <summary>How a routed command runs: the handler instance plus the parsed command.</summary>
    private delegate Task Route(CommandHandlers handlers, long userId, long chatId, BotCommand cmd, CancellationToken ct);

    /// <summary>The canonical verbs the table answers — the drift test's view of it.</summary>
    public static IReadOnlyCollection<string> HandledVerbs => Routes.Keys;

    /// <summary>Route a parsed command to its handler (unknown verbs fall back to a /help hint).</summary>
    public Task DispatchAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct) =>
        Routes.TryGetValue(BotCommands.Canonical(cmd.Verb), out Route? route)
            ? route(this, userId, chatId, cmd, ct)
            : UnknownAsync(chatId, ct);
}
