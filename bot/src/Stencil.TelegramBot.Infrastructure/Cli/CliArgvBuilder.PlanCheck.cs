namespace Stencil.TelegramBot.Infrastructure.Cli;

public static partial class CliArgvBuilder
{
    private const string _flagPlanCheck = "--plan-check";
    private const string _flagPlanSurface = "--plan-surface";
    private const string _stdin = "-";

    // Whose profile, surfaceKeys and forbidden policy judge a plan (cli/CONTRACT.md §7).
    public const string PLAN_SURFACE = "bot";

    // cli/CONTRACT.md §7: the reply rides stdin, so neither a path nor model text reaches argv.
    public static IReadOnlyList<string> BuildPlanCheckArgv() =>
        [_flagPlanCheck, _stdin, _flagPlanSurface, PLAN_SURFACE];
}
