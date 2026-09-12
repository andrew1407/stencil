using System.Text.Json.Serialization;

namespace Stencil.TelegramBot.Domain.Sessions;

// Serialised BY NAME so a stored session survives reordering; mirrors connectionManager.js.
[JsonConverter(typeof(JsonStringEnumConverter))]
public enum CredentialKind
{
    None = 0,

    // It listed projects directly, so it is not an admin token.
    Session = 1,

    // It can't list projects, but it PROVED it can mint a session token.
    Admin = 2,
}
