using System.Net;
using System.Net.Sockets;
using System.Text;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Net;
using Stencil.TelegramBot.Infrastructure.Server;

namespace Stencil.TelegramBot.Tests.Server;

/// <summary>The pooled handlers behind every server client, against real loopback sockets (nothing leaves the machine): a 3xx is answered, never followed, so a vetted server cannot bounce a request to an address <c>/connect</c> refused; the dial-time guard judges every address; the LLM client's handler follows no redirect either.</summary>
public sealed class StencilServerClientFactoryTests
{
    private const string _okProjects =
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 15\r\nConnection: close\r\n\r\n{\"projects\":[]}";

    private static string redirectTo(string url) =>
        $"HTTP/1.1 302 Found\r\nLocation: {url}\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";

    [Fact]
    public async Task Should_Answer_A_Redirect_Instead_Of_Following_It()
    {
        await using LoopbackPeer target = new(_okProjects);
        await using LoopbackPeer bouncer = new(redirectTo(target.Url + "/projects"));
        IStencilServerClient client = new StencilServerClientFactory(new BotOptions()).Create(bouncer.Url, "t");

        ServerException ex = await Assert.ThrowsAsync<ServerException>(() => client.ListProjectsAsync());

        Assert.Equal(302, ex.Status);
        Assert.Equal(0, target.Connections);
    }

    [Fact]
    public async Task Should_Surface_The_Dial_Time_Refusal_Verbatim()
    {
        StencilServerClientFactory factory = new(new BotOptions(), isBlockedAddress: _ => true);
        IStencilServerClient client = factory.Create("http://93.184.216.34:8090", "t");

        InvalidOperationException ex = await Assert.ThrowsAsync<InvalidOperationException>(
            () => client.ListProjectsAsync());

        Assert.Equal("That server address isn't allowed.", ex.Message);
    }

    [Fact]
    public async Task Should_Reach_A_Loopback_Server_Unless_The_Operator_Allows_Only_Public_Ones()
    {
        await using LoopbackPeer server = new(_okProjects);
        StencilServerClientFactory allowing = new(new BotOptions(), RemoteImageUrl.ServerAddressGuard(allowPrivate: true));
        StencilServerClientFactory publicOnly = new(new BotOptions(), RemoteImageUrl.ServerAddressGuard(allowPrivate: false));

        Assert.Empty(await allowing.Create(server.Url, "t").ListProjectsAsync());
        await Assert.ThrowsAsync<InvalidOperationException>(() => publicOnly.Create(server.Url, "t").ListProjectsAsync());
    }

    [Fact]
    public async Task Should_Not_Follow_A_Redirect_Or_Buffer_Without_Bound_On_The_Llm_Client()
    {
        await using LoopbackPeer target = new(_okProjects);
        await using LoopbackPeer bouncer = new(redirectTo(target.Url));
        BotOptions options = new();
        using HttpClient http = new StencilServerClientFactory(options).CreateHttpClient(TimeSpan.FromSeconds(10));

        using HttpResponseMessage response = await http.GetAsync(bouncer.Url);

        Assert.Equal(HttpStatusCode.Found, response.StatusCode);
        Assert.Equal(0, target.Connections);
        Assert.Equal(options.MaxServerResponseBytes, http.MaxResponseContentBufferSize);
    }

    [Fact]
    public async Task Should_Report_A_Closed_Port_As_Unreachable_Not_Refused()
    {
        TcpListener probe = new(IPAddress.Loopback, 0);
        probe.Start();
        int port = ((IPEndPoint)probe.LocalEndpoint).Port;
        probe.Stop(); // nothing listens there now
        StencilServerClientFactory factory = new(new BotOptions(), RemoteImageUrl.ServerAddressGuard(allowPrivate: true));

        ServerException ex = await Assert.ThrowsAsync<ServerException>(
            () => factory.Create($"http://127.0.0.1:{port}", "t").ListProjectsAsync());

        Assert.Equal("unreachable", ex.Code);
    }

    // A proxy would be dialled instead of the target, so the guard would judge the proxy's address.
    [Fact]
    public void Should_Bypass_Any_Proxy_Only_Where_A_Guard_Judges_The_Address()
    {
        Assert.False(GuardedConnect.Handler(_ => false, "refused").UseProxy);
        Assert.True(GuardedConnect.Handler(null, "refused").UseProxy);
    }

    [Fact]
    public async Task Should_Guard_The_Llm_Client_For_A_User_Connected_Server()
    {
        StencilServerClientFactory factory = new(new BotOptions(), isBlockedAddress: _ => true);
        using HttpClient http = factory.CreateGuardedHttpClient(TimeSpan.FromSeconds(10));

        HttpRequestException ex = await Assert.ThrowsAsync<HttpRequestException>(
            () => http.GetAsync("http://93.184.216.34:8090/llm/chat"));

        Assert.Equal("That server address isn't allowed.", GuardedConnect.RefusalIn(ex)?.Message);
    }

    /// <summary>A loopback HTTP peer that answers every request with one canned response and counts connections.</summary>
    private sealed class LoopbackPeer : IAsyncDisposable
    {
        private readonly TcpListener _listener = new(IPAddress.Loopback, 0);
        private readonly byte[] _response;
        private readonly Task _loop;
        private int _connections;

        public LoopbackPeer(string response)
        {
            _response = Encoding.ASCII.GetBytes(response);
            _listener.Start();
            _loop = Task.Run(serveAsync);
        }

        public string Url => $"http://127.0.0.1:{((IPEndPoint)_listener.LocalEndpoint).Port}";

        public int Connections => Volatile.Read(ref _connections);

        private async Task serveAsync()
        {
            try
            {
                while (true)
                {
                    using TcpClient client = await _listener.AcceptTcpClientAsync();
                    Interlocked.Increment(ref _connections);
                    NetworkStream stream = client.GetStream();
                    byte[] buffer = new byte[4096];
                    StringBuilder request = new();
                    int read;
                    while (!request.ToString().Contains("\r\n\r\n") && (read = await stream.ReadAsync(buffer)) > 0)
                    {
                        request.Append(Encoding.ASCII.GetString(buffer, 0, read));
                    }
                    await stream.WriteAsync(_response);
                }
            }
            catch (Exception)
            {
                // The listener stopped.
            }
        }

        public async ValueTask DisposeAsync()
        {
            _listener.Stop();
            await _loop;
        }
    }
}
