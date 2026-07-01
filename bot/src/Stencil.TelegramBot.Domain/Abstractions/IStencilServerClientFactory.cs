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
    /// <see cref="IStencilServerClient.ConnectAsync"/>.
    /// </summary>
    IStencilServerClient Create(string url, string? token = null, bool verifyTls = true);

    /// <summary>Normalise a raw URL to a stable origin (<c>scheme://host[:port]</c>).</summary>
    string NormalizeUrl(string url);
}
