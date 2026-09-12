namespace Stencil.TelegramBot.Domain.Exceptions;

// protocol ErrorResponse {code, message} plus the status; Code is conflict/notFound/unauthorized.
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

    public bool IsConflict => Status == 409 || Code == "conflict";
}
