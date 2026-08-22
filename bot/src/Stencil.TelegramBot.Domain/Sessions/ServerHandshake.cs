namespace Stencil.TelegramBot.Domain.Sessions;

/// <summary>
/// What a connect handshake resolved to: the effective bearer token plus what the supplied
/// credential proved to be, so the caller can persist both on the connection record.
/// </summary>
public readonly record struct ServerHandshake(string Token, CredentialKind CredentialKind);
