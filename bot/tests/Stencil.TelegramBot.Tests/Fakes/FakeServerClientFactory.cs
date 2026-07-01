using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Infrastructure.Server;

namespace Stencil.TelegramBot.Tests.Fakes;

/// <summary>
/// An <see cref="IStencilServerClientFactory"/> over in-memory <see cref="FakeStencilServerClient"/>s,
/// keyed by normalised origin. Reuses the real <see cref="UrlNormalizer"/> so dedupe/keying
/// matches production; records every <see cref="Create"/> for assertions.
/// </summary>
public sealed class FakeServerClientFactory : IStencilServerClientFactory
{
    private readonly Dictionary<string, FakeStencilServerClient> _clients = new();

    /// <summary>Every <see cref="Create"/> call (normalised url, token, TLS choice), in order.</summary>
    public List<(string Url, string? Token, bool VerifyTls)> Created { get; } = new();

    /// <summary>Get (or lazily make) the fake client for <paramref name="url"/>'s origin.</summary>
    public FakeStencilServerClient ClientFor(string url)
    {
        string normalized = NormalizeUrl(url);
        if (!_clients.TryGetValue(normalized, out FakeStencilServerClient? client))
        {
            client = new FakeStencilServerClient(normalized);
            _clients[normalized] = client;
        }
        return client;
    }

    /// <inheritdoc />
    public IStencilServerClient Create(string url, string? token = null, bool verifyTls = true)
    {
        string normalized = NormalizeUrl(url);
        Created.Add((normalized, token, verifyTls));
        return ClientFor(normalized);
    }

    /// <inheritdoc />
    public string NormalizeUrl(string url) => UrlNormalizer.Normalize(url);
}
