using System.Text.Json.Serialization;

namespace Stencil.TelegramBot.Domain.Sessions;

// The browser's credentialKind (connectionManager.js), persisted so a later connect can skip a
// doomed probe. Serialised BY NAME, so a stored session survives reordering these.
[JsonConverter(typeof(JsonStringEnumConverter))]
public enum CredentialKind
{
    // Nothing supplied: the session token was minted anonymously.
    None = 0,

    // It listed projects directly, so it is not an admin token.
    Session = 1,

    // It can't list projects, but it PROVED it can mint a session token.
    Admin = 2,
}
