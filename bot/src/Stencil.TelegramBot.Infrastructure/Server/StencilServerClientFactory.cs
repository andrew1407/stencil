using System.Net;
using System.Net.Security;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Net;

namespace Stencil.TelegramBot.Infrastructure.Server;

// Mirrors pystencil's ConnectionManager construction (verify=False ⇒ unverified SSL context).
public sealed class StencilServerClientFactory : IStencilServerClientFactory
{
    private const string _refusal = "That server address isn't allowed.";

    // One pooled SocketsHttpHandler per TLS choice: the clients are transient and never disposed,
    // so a per-call handler would leak its whole connection pool.
    private readonly Lazy<SocketsHttpHandler> _verifying;
    private readonly Lazy<SocketsHttpHandler> _insecure;
    private readonly Lazy<SocketsHttpHandler> _operator = new(() => GuardedConnect.Handler(null, _refusal));
    private readonly BotOptions _options;

    // isBlockedAddress judges every address a server client dials, so a /connect-vetted host can
    // neither redirect nor rebind to one the vetting refused.
    public StencilServerClientFactory(BotOptions options, Func<IPAddress, bool>? isBlockedAddress = null)
    {
        _options = options;
        _verifying = new(() => GuardedConnect.Handler(isBlockedAddress, _refusal));
        _insecure = new(() => trustAnyCertificate(GuardedConnect.Handler(isBlockedAddress, _refusal)));
    }

    // The client carries the configured timeout so a slow server can't block a handler
    // indefinitely.
    public IStencilServerClient Create(string url, string? token = null, bool verifyTls = true, string? credential = null,
        CredentialKind credentialKind = CredentialKind.NONE)
    {
        SocketsHttpHandler handler = (verifyTls ? _verifying : _insecure).Value;
        HttpClient http = new(handler, disposeHandler: false)
        {
            Timeout = _options.ServerHttpTimeout,
        };
        return new HttpStencilServerClient(http, NormalizeUrl(url), token, credential, credentialKind)
        {
            MaxResponseBytes = _options.MaxServerResponseBytes,
            ProjectListLimit = _options.ProjectListLimit,
        };
    }

    public string NormalizeUrl(string url) => UrlNormalizer.Normalize(url);

    // For the LLM client, whose endpoint is the operator's own (often a loopback model), so it skips
    // the address guard but still never follows a redirect.
    public HttpClient CreateHttpClient(TimeSpan timeout) =>
        new(_operator.Value, disposeHandler: false)
        {
            Timeout = timeout,
            MaxResponseContentBufferSize = _options.MaxServerResponseBytes,
        };

    private static SocketsHttpHandler trustAnyCertificate(SocketsHttpHandler handler)
    {
        handler.SslOptions = new SslClientAuthenticationOptions
        {
            RemoteCertificateValidationCallback = static (_, _, _, _) => true,
        };
        return handler;
    }
}
