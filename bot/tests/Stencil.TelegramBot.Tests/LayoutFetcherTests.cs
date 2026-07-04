using System.Net;
using System.Text;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Links;
using Stencil.TelegramBot.Tests.Fakes;
using Xunit;

namespace Stencil.TelegramBot.Tests;

/// <summary>The /layout URL fetcher: body pass-through, non-success → null, byte cap.</summary>
public sealed class LayoutFetcherTests
{
    private static LayoutFetcher Make(Func<HttpRequestMessage, byte[], HttpResponseMessage> responder,
        long? maxBytes = null)
    {
        BotOptions options = maxBytes is long cap
            ? new BotOptions { MaxDownloadBytes = cap }
            : new BotOptions();
        return new LayoutFetcher(options, new CannedHttpMessageHandler(responder));
    }

    [Fact]
    public async Task FetchReturnsTheBody()
    {
        const string json = "{\"imageWidth\":1,\"imageHeight\":2,\"lines\":[]}";
        using LayoutFetcher fetcher = Make((_, _) => CannedHttpMessageHandler.Json(json));
        byte[]? body = await fetcher.FetchAsync("https://layouts.example/a.json");
        Assert.Equal(json, Encoding.UTF8.GetString(body!));
    }

    [Fact]
    public async Task FetchReturnsNullOnANonSuccessStatus()
    {
        using LayoutFetcher fetcher = Make((_, _) => CannedHttpMessageHandler.Empty(HttpStatusCode.NotFound));
        Assert.Null(await fetcher.FetchAsync("https://layouts.example/missing.json"));
    }

    [Fact]
    public async Task FetchRefusesRedirects()
    {
        // The URL is SSRF-vetted before the fetch; a redirect could bounce the request to
        // a private/metadata host the guard would have rejected, so 3xx yields null.
        using LayoutFetcher fetcher = Make((_, _) =>
        {
            HttpResponseMessage redirect = CannedHttpMessageHandler.Empty(HttpStatusCode.Redirect);
            redirect.Headers.Location = new Uri("http://169.254.169.254/latest/meta-data/");
            return redirect;
        });
        Assert.Null(await fetcher.FetchAsync("https://layouts.example/bounce.json"));
    }

    [Fact]
    public async Task FetchThrowsPastTheByteCap()
    {
        string big = new('x', 64);
        using LayoutFetcher fetcher = Make((_, _) => CannedHttpMessageHandler.Json(big), maxBytes: 16);
        await Assert.ThrowsAsync<InvalidOperationException>(
            () => fetcher.FetchAsync("https://layouts.example/huge.json"));
    }

    [Fact]
    public async Task FetchRejectsAnAddressTheConnectGuardBlocks()
    {
        // No canned handler → the real connect-time guard runs. A literal-IP host needs no
        // DNS, so the always-block predicate rejects it before any socket connect.
        using LayoutFetcher fetcher = new(new BotOptions(), isBlockedAddress: _ => true);
        InvalidOperationException ex = await Assert.ThrowsAsync<InvalidOperationException>(
            () => fetcher.FetchAsync("https://203.0.113.9/a.json"));
        Assert.Contains("private or local address", ex.Message);
    }
}
