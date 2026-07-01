namespace Stencil.TelegramBot.Domain.Exceptions;

/// <summary>
/// A Stencil CLI invocation failed (non-zero exit, or success with no parseable
/// <c>wrote</c> line). The message carries the CLI's <c>error: …</c> stderr text, mirroring
/// <c>mcp/src/outcome.rs</c> <c>extract_errors</c>.
/// </summary>
public sealed class StencilCliException : Exception
{
    public StencilCliException(string message) : base(message) { }
}
