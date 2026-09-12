using System.Text.Json.Serialization;

namespace Stencil.TelegramBot.Domain.Sessions;

// Serialised BY NAME so a stored session survives reordering; mirrors connectionManager.js.
[JsonConverter(typeof(JsonStringEnumConverter))]
public enum CredentialKind
{
    [JsonStringEnumMemberName("None")]
    NONE = 0,

    // It listed projects directly, so it is not an admin token.
    [JsonStringEnumMemberName("Session")]
    SESSION = 1,

    // It can't list projects, but it PROVED it can mint a session token.
    [JsonStringEnumMemberName("Admin")]
    ADMIN = 2,
}
