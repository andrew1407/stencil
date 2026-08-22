using System.Text.Json.Serialization;

namespace Stencil.TelegramBot.Domain.Sessions;

/// <summary>
/// What a connection's credential turned out to be — the browser's <c>credentialKind</c>
/// (<c>connectionManager.js</c>), persisted so a later connect can skip a doomed probe.
/// </summary>
/// <remarks>
/// Serialised by name so a stored session survives any reordering of the members.
/// </remarks>
[JsonConverter(typeof(JsonStringEnumConverter))]
public enum CredentialKind
{
    /// <summary>Nothing was supplied — the session token was minted anonymously.</summary>
    None = 0,

    /// <summary>A session token: it listed projects directly, so it is not an admin token.</summary>
    Session = 1,

    /// <summary>An ADMIN token: it can't list projects, but it PROVED it can mint a session token.</summary>
    Admin = 2,
}
