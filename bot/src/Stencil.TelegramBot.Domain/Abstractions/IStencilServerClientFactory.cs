using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Domain.Abstractions;

public interface IStencilServerClientFactory
{
    // credential: the user's connect value, re-minted once when the session token goes stale.
    IStencilServerClient Create(string url, string? token = null, bool verifyTls = true, string? credential = null,
        CredentialKind credentialKind = CredentialKind.NONE);

    string NormalizeUrl(string url);
}
