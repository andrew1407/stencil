namespace Stencil.TelegramBot.Domain.Sessions;

/// <summary>
/// A remembered collaboration-server connection for one user: a normalised origin
/// (<c>scheme://host[:port]</c>) plus the bearer token minted/validated at connect time.
/// </summary>
/// <remarks>
/// Mirrors <c>pystencil</c>'s REST-only <c>ServerConnection</c> identity — a connection is
/// just a validated token + base URL (no live WS feed). <see cref="VerifyTls"/> false
/// accepts self-signed certs for dev servers.
/// </remarks>
public sealed record ServerConnectionInfo
{
    public required string Url { get; init; }
    public string Token { get; init; } = "";
    public bool VerifyTls { get; init; } = true;
}
