using System.Net.Security;
using Stencil.TelegramBot.Domain.Abstractions;

namespace Stencil.TelegramBot.Infrastructure.Server;

/// <summary>
/// Builds a <see cref="IStencilServerClient"/> for a given server origin, centralising URL
/// normalisation and the TLS-verification choice (the dev-server self-signed-cert escape
/// hatch). Mirrors <c>pystencil</c>'s <c>ConnectionManager</c> construction
/// (<c>verify=False ⇒ unverified SSL context</c>).
/// </summary>
public sealed class StencilServerClientFactory : IStencilServerClientFactory
{
    // SocketsHttpHandler is thread-safe and pools its connections, so one handler is shared by
    // every client (per TLS choice) instead of allocated per Create — the clients are transient
    // and never disposed, so a per-call handler would leak its whole connection pool.
    private readonly Lazy<SocketsHttpHandler> _verifying = new(() => new SocketsHttpHandler());
    private readonly Lazy<SocketsHttpHandler> _insecure = new(CreateInsecureHandler);

    /// <summary>
    /// Create a client for <paramref name="url"/> over an <see cref="HttpClient"/> whose server
    /// certificate validation is bypassed when <paramref name="verifyTls"/> is false.
    /// </summary>
    public IStencilServerClient Create(string url, string? token = null, bool verifyTls = true)
    {
        SocketsHttpHandler handler = (verifyTls ? _verifying : _insecure).Value;
        HttpClient http = new(handler, disposeHandler: false);
        return new HttpStencilServerClient(http, NormalizeUrl(url), token);
    }

    /// <summary>Normalise a raw URL to a stable origin (<c>scheme://host[:port]</c>).</summary>
    public string NormalizeUrl(string url) => UrlNormalizer.Normalize(url);

    private static SocketsHttpHandler CreateInsecureHandler() => new()
    {
        SslOptions = new SslClientAuthenticationOptions
        {
            RemoteCertificateValidationCallback = static (_, _, _, _) => true,
        },
    };
}
