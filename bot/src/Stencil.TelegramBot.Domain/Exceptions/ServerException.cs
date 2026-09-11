namespace Stencil.TelegramBot.Domain.Exceptions;

// A non-2xx REST response: the server's structured {code, message} (protocol ErrorResponse)
// plus the raw status, mirroring pystencil's ServerError. Code is conflict/notFound/unauthorized.
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
