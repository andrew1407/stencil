namespace Stencil.TelegramBot.Domain.Exceptions;

// Carries the CLI's `error: …` stderr text (the argv/stderr contract mcp's extract_errors also
// reads).
public sealed class StencilCliException : Exception
{
    public StencilCliException(string message) : base(message) { }

    // Binary paths, env vars, raw stderr: the operator's log only, never the chat reply.
    public string? OperatorDetail { get; private init; }

    public static StencilCliException Deployment(string message, string operatorDetail) =>
        new(message) { OperatorDetail = operatorDetail };
}
