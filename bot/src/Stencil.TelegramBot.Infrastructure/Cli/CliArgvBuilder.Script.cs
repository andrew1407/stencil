using Stencil.TelegramBot.Domain.Exceptions;

namespace Stencil.TelegramBot.Infrastructure.Cli;

public static partial class CliArgvBuilder
{
    private const string _flagScriptPlan = "--script-plan";

    // cli/CONTRACT.md §5. Plan mode fetches nothing, decodes nothing and writes nothing, so there
    // is no output to confine — only the script leaf and the frame the lengths resolve against.
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
