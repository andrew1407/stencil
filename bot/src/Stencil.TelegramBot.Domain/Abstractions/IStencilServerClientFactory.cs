using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Domain.Abstractions;

/// <summary>
/// Builds a <see cref="IStencilServerClient"/> for a given server origin. Centralises URL
/// normalisation and the TLS-verification choice (the dev-server self-signed-cert escape
/// hatch), mirroring <c>pystencil</c>'s <c>ConnectionManager</c> construction.
/// </summary>
public interface IStencilServerClientFactory
{
    /// <summary>
    /// Create a client for <paramref name="url"/> (normalised internally). Pass a known
    /// <paramref name="token"/> to reuse it, or null to mint one on
    /// <see cref="IStencilServerClient.ConnectAsync"/>. <paramref name="credential"/> is the
    /// user-supplied connect value, used to re-mint once when the session token goes stale, and
    /// <paramref name="credentialKind"/> what a previous handshake proved it to be.
    /// </summary>
    IStencilServerClient Create(string url, string? token = null, bool verifyTls = true, string? credential = null,
        CredentialKind credentialKind = CredentialKind.None);

    /// <summary>Normalise a raw URL to a stable origin (<c>scheme://host[:port]</c>).</summary>
    string NormalizeUrl(string url);
}
