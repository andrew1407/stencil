namespace Stencil.TelegramBot.Domain.Exceptions;

// A non-zero exit, or a success with no parseable `wrote` line. The message carries the CLI's
// `error: …` stderr text, mirroring mcp's extract_errors.
public sealed class StencilCliException : Exception
{
    public StencilCliException(string message) : base(message) { }

    // Binary paths, env vars, raw stderr: the operator's log only, never the chat reply.
    public string? OperatorDetail { get; private init; }

    public static StencilCliException Deployment(string message, string operatorDetail) =>
        new(message) { OperatorDetail = operatorDetail };
}
