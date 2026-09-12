namespace Stencil.TelegramBot.Domain.Sessions;

public sealed record ServerConnectionInfo
{
    public required string Url { get; init; }
    public string Token { get; init; } = "";

    // The user-supplied connect value, kept apart from the session token so a stale one can be
    // re-minted.
    public string Credential { get; init; } = "";

    public CredentialKind CredentialKind { get; init; } = CredentialKind.NONE;

    public bool VerifyTls { get; init; } = true;
}
