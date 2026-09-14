namespace Stencil.TelegramBot.Application.Llm;

// What one `.stc` run produced. Renders are EXTRA images (one per block that loaded its own
// source); a Mutated run's main result goes through the caller's usual render path.
public sealed record ScriptOutcome(
    string Reply,
    IReadOnlyList<string> Warnings,
    IReadOnlyList<PromptRender> Renders,
    bool Mutated = false);

public interface IScriptService
{
    // Lowers the script through the CLI and applies the result to the user's working image.
    Task<ScriptOutcome> RunAsync(long userId, string scriptText, CancellationToken ct = default);
}
