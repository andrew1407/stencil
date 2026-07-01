namespace Stencil.TelegramBot.Domain.Exceptions;

/// <summary>
/// A non-2xx REST response from a collaboration server. Carries the server's structured
/// <c>{code, message}</c> (protocol <c>ErrorResponse</c>) plus the raw HTTP status, mirroring
/// <c>pystencil</c>'s <c>ServerError</c>. <see cref="Code"/> is e.g. <c>conflict</c>,
/// <c>notFound</c>, <c>unauthorized</c>.
/// </summary>
public sealed class ServerException : Exception
{
    public string Code { get; }
    public int? Status { get; }

    public ServerException(string code, string message, int? status = null)
        : base(string.IsNullOrEmpty(code) ? message : $"{code}: {message}")
    {
        Code = code;
        Status = status;
    }

    /// <summary>True when this is a last-writer-wins conflict (HTTP 409 / <c>conflict</c>).</summary>
    public bool IsConflict => Status == 409 || Code == "conflict";
}
