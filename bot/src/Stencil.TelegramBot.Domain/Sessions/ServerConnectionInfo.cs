namespace Stencil.TelegramBot.Domain.Sessions;

// pystencil's REST-only ServerConnection identity: a validated token + a normalised origin,
// no live WS feed. VerifyTls false accepts a dev server's self-signed cert.
public sealed record ServerConnectionInfo
{
    public required string Url { get; init; }
    public string Token { get; init; } = "";

    // The user-supplied connect value (possibly the server's ADMIN token), kept apart from the
    // live session token so a stale session can be re-minted.
    public string Credential { get; init; } = "";

    public CredentialKind CredentialKind { get; init; } = CredentialKind.None;

    public bool VerifyTls { get; init; } = true;
}
