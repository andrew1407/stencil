using System.Net;
using System.Text;

namespace Stencil.TelegramBot.Tests.Fakes;

/// <summary>
/// A test <see cref="HttpMessageHandler"/> that captures the outgoing request (method, URI,
/// headers and fully-buffered body) and returns a canned response produced by a responder
/// delegate. Lets <c>HttpStencilServerClient</c> be exercised with zero network.
/// </summary>
public sealed class CannedHttpMessageHandler : HttpMessageHandler
{
    private readonly Func<HttpRequestMessage, byte[], HttpResponseMessage> _responder;

    public CannedHttpMessageHandler(Func<HttpRequestMessage, byte[], HttpResponseMessage> responder)
    {
        _responder = responder;
    }

    /// <summary>The last request the handler saw.</summary>
    public HttpRequestMessage? LastRequest { get; private set; }

    /// <summary>The last request's buffered body bytes (empty when there was no content).</summary>
    public byte[] LastBody { get; private set; } = Array.Empty<byte>();

    /// <summary>The content-type of the last request's body, if any.</summary>
    public string? LastContentType { get; private set; }

    /// <summary>Build a JSON <c>200 OK</c> response from a raw JSON string.</summary>
    public static HttpResponseMessage Json(string json, HttpStatusCode status = HttpStatusCode.OK) =>
        new(status)
        {
            Content = new StringContent(json, Encoding.UTF8, "application/json"),
        };

    /// <summary>Build an empty-body response with the given status.</summary>
    public static HttpResponseMessage Empty(HttpStatusCode status) =>
        new(status)
        {
            Content = new ByteArrayContent(Array.Empty<byte>()),
        };

    protected override async Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
    {
        byte[] body = Array.Empty<byte>();
        if (request.Content is not null)
        {
            body = await request.Content.ReadAsByteArrayAsync(cancellationToken).ConfigureAwait(false);
            LastContentType = request.Content.Headers.ContentType?.MediaType;
        }
        LastRequest = request;
        LastBody = body;
        return _responder(request, body);
    }
}
