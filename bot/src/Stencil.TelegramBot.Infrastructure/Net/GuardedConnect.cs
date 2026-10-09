using System.Net;
using System.Net.Sockets;

namespace Stencil.TelegramBot.Infrastructure.Net;

// A handler that never follows a redirect and, given a predicate, resolves the host itself and dials
// only an address it passes, so nothing rebinds between a URL's vetting and its connect.
public static class GuardedConnect
{
    public static SocketsHttpHandler Handler(Func<IPAddress, bool>? isBlockedAddress, string refusal)
    {
        SocketsHttpHandler handler = new() { AllowAutoRedirect = false };
        if (isBlockedAddress is null)
        {
            return handler;
        }
        // A proxy would make the guard judge the proxy's address while the proxy dials the target.
        handler.UseProxy = false;
        handler.ConnectCallback = async (context, ct) =>
        {
            DnsEndPoint dns = context.DnsEndPoint;
            IReadOnlyList<IPAddress> addresses = IPAddress.TryParse(dns.Host, out IPAddress? literal)
                ? new[] { literal }
                : await Dns.GetHostAddressesAsync(dns.Host, ct);
            Exception? unreachable = null;
            foreach (IPAddress address in addresses)
            {
                if (isBlockedAddress(address))
                {
                    continue;
                }
                Socket socket = new(address.AddressFamily, SocketType.Stream, ProtocolType.Tcp)
                {
                    NoDelay = true,
                };
                try
                {
                    await socket.ConnectAsync(new IPEndPoint(address, dns.Port), ct);
                    return new NetworkStream(socket, ownsSocket: true);
                }
                catch (Exception ex)
                {
                    socket.Dispose();
                    if (ex is OperationCanceledException)
                    {
                        throw;
                    }
                    unreachable = ex;
                }
            }
            // Only a host with no allowed address is refused; one whose allowed address failed is unreachable.
            throw unreachable ?? new InvalidOperationException(refusal);
        };
        return handler;
    }

    // The refusal under the transport error that wraps it, to be surfaced verbatim.
    public static InvalidOperationException? RefusalIn(Exception ex)
    {
        for (Exception? e = ex.InnerException; e is not null; e = e.InnerException)
        {
            if (e is InvalidOperationException refused)
            {
                return refused;
            }
        }
        return null;
    }
}
