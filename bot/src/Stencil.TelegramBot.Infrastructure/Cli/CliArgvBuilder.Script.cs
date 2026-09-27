using Stencil.TelegramBot.Domain.Exceptions;

namespace Stencil.TelegramBot.Infrastructure.Cli;

public static partial class CliArgvBuilder
{
    private const string _flagScriptPlan = "--script-plan";

    // cli/CONTRACT.md §4.3. Plan mode writes nothing, so there is no output to confine; each chunk
    // comes back with core's verdict under the bot's surface.
    public static IReadOnlyList<string> BuildScriptPlanArgv(string script, string? input = null)
    {
        List<string> argv = new();
        if (input is { Length: > 0 })
        {
            argv.Add(_flagInput);
            argv.Add(guard(input, "input"));
        }
        argv.Add(_flagScriptPlan);
        argv.Add(guard(script, "script"));
        argv.Add(_flagPlanSurface);
        argv.Add(PLAN_SURFACE);
        return argv;
    }

    // The CLI has no `--` terminator, so a dash-leading positional would parse as a flag.
    private static string guard(string value, string what)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            throw new StencilCliException($"`{what}` must not be empty");
        }
        if (value.StartsWith('-'))
        {
            throw new StencilCliException(
                $"`{what}` must not start with '-' (got \"{value}\") — a dash-leading value would be "
                + "parsed as a CLI flag");
        }
        return value;
    }
}
