namespace Stencil.TelegramBot.Domain.Exceptions;

/// <summary>
/// A Stencil CLI invocation failed (non-zero exit, or success with no parseable
/// <c>wrote</c> line). The message carries the CLI's <c>error: …</c> stderr text, mirroring
/// <c>mcp/src/outcome.rs</c> <c>extract_errors</c>.
/// </summary>
public sealed class StencilCliException : Exception
{
    public StencilCliException(string message) : base(message) { }

    /// <summary>
    /// Deployment detail (binary paths, env vars, raw stderr) for the operator's log only —
    /// the chat reply is <see cref="Exception.Message"/>. Null when there is nothing to hide.
    /// </summary>
    public string? OperatorDetail { get; private init; }

    /// <summary>A failure whose real cause is operator-only: plain message out, detail logged.</summary>
    public static StencilCliException Deployment(string message, string operatorDetail) =>
        new(message) { OperatorDetail = operatorDetail };
}
