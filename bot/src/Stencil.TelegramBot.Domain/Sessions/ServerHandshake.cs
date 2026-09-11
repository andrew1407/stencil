namespace Stencil.TelegramBot.Domain.Sessions;

// What a connect handshake resolved to; the caller persists both onto the connection record.
public readonly record struct ServerHandshake(string Token, CredentialKind CredentialKind);
