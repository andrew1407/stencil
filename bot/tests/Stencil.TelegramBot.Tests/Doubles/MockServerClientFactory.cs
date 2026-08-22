using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Server;

namespace Stencil.TelegramBot.Tests.Doubles;

/// <summary>
/// An <see cref="IStencilServerClientFactory"/> over in-memory <see cref="MockStencilServerClient"/>s,
/// keyed by normalised origin. Reuses the real <see cref="UrlNormalizer"/> so dedupe/keying
/// matches production; records every <see cref="Create"/> for assertions.
/// </summary>
public sealed class MockServerClientFactory : IStencilServerClientFactory
{
    private readonly Dictionary<string, MockStencilServerClient> _clients = new();

    /// <summary>Every <see cref="Create"/> call (normalised url, token, TLS choice, credential,
    /// credential kind), in order.</summary>
    public List<(string Url, string? Token, bool VerifyTls, string? Credential, CredentialKind Kind)> Created { get; } = new();

    /// <summary>Get (or lazily make) the mock client for <paramref name="url"/>'s origin.</summary>
    public MockStencilServerClient ClientFor(string url)
    {
        string normalized = NormalizeUrl(url);
        if (!_clients.TryGetValue(normalized, out MockStencilServerClient? client))
        {
            client = new MockStencilServerClient(normalized);
            _clients[normalized] = client;
        }
        return client;
    }

    /// <inheritdoc />
    public IStencilServerClient Create(string url, string? token = null, bool verifyTls = true, string? credential = null,
        CredentialKind credentialKind = CredentialKind.None)
    {
        string normalized = NormalizeUrl(url);
        Created.Add((normalized, token, verifyTls, credential, credentialKind));
        return ClientFor(normalized);
    }

    /// <inheritdoc />
    public string NormalizeUrl(string url) => UrlNormalizer.Normalize(url);
}
