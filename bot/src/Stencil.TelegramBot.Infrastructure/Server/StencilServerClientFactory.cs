using System.Net.Security;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Configuration;

namespace Stencil.TelegramBot.Infrastructure.Server;

// Mirrors pystencil's ConnectionManager construction (verify=False ⇒ unverified SSL context).
public sealed class StencilServerClientFactory : IStencilServerClientFactory
{
    // One pooled SocketsHttpHandler per TLS choice: the clients are transient and never disposed,
    // so a per-call handler would leak its whole connection pool.
    private readonly Lazy<SocketsHttpHandler> _verifying = new(() => new SocketsHttpHandler());
    private readonly Lazy<SocketsHttpHandler> _insecure = new(createInsecureHandler);
    private readonly TimeSpan _timeout;

    public StencilServerClientFactory(BotOptions options)
    {
        _timeout = options.ServerHttpTimeout;
    }

    // The client carries the configured timeout so a slow server can't block a handler
    // indefinitely.
    public IStencilServerClient Create(string url, string? token = null, bool verifyTls = true, string? credential = null,
        CredentialKind credentialKind = CredentialKind.NONE)
    {
        SocketsHttpHandler handler = (verifyTls ? _verifying : _insecure).Value;
        HttpClient http = new(handler, disposeHandler: false)
        {
            Timeout = _timeout,
        };
        return new HttpStencilServerClient(http, NormalizeUrl(url), token, credential, credentialKind);
    }

    public string NormalizeUrl(string url) => UrlNormalizer.Normalize(url);

    // For adapters that speak their own HTTP (the LLM client), so they reuse this pool instead of
    // growing their own.
    public HttpClient CreateHttpClient(TimeSpan timeout) =>
        new(_verifying.Value, disposeHandler: false) { Timeout = timeout };

    private static SocketsHttpHandler createInsecureHandler() => new()
    {
        SslOptions = new SslClientAuthenticationOptions
        {
            RemoteCertificateValidationCallback = static (_, _, _, _) => true,
        },
    };
}
