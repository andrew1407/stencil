namespace Stencil.TelegramBot.Domain.Sessions;

public readonly record struct ServerHandshake(string Token, CredentialKind CredentialKind);
