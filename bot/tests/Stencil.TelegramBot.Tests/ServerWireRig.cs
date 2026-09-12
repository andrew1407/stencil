using Stencil.TelegramBot.Infrastructure.Server;
using Stencil.TelegramBot.Tests.Doubles;

namespace Stencil.TelegramBot.Tests;

/// <summary>The shared rig for the <see cref="HttpStencilServerClient"/> wire suites: a client
/// over a captured-request mock handler, no network.</summary>
internal static class ServerWireRig
{
    internal static HttpStencilServerClient Client(CannedHttpMessageHandler handler, string? token = "") =>
        new(new HttpClient(handler), "http://h:8090", token);
}
