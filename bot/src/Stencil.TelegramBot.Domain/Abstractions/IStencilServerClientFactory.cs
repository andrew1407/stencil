using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Domain.Abstractions;

// Centralises URL normalisation and the TLS-verification choice (the dev-server self-signed
// escape hatch), mirroring pystencil's ConnectionManager construction.
public interface IStencilServerClientFactory
{
    // url is normalised internally; a null token is minted on ConnectAsync. credential is the
    // user-supplied connect value, used to re-mint once when the session token goes stale.
    IStencilServerClient Create(string url, string? token = null, bool verifyTls = true, string? credential = null,
        CredentialKind credentialKind = CredentialKind.None);

    // To a stable origin: scheme://host[:port].
    string NormalizeUrl(string url);
}
