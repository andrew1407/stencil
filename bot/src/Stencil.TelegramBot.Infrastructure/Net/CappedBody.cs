namespace Stencil.TelegramBot.Infrastructure.Net;

// A response body read whole but never past max bytes; an honest Content-Length over it is refused
// unread.
public static class CappedBody
{
    // Null past max.
    public static async Task<byte[]?> ReadAsync(HttpContent content, long max, CancellationToken ct)
    {
        if (content.Headers.ContentLength > max)
        {
            return null;
        }
        await using Stream body = await content.ReadAsStreamAsync(ct).ConfigureAwait(false);
        using MemoryStream buffer = new();
        byte[] chunk = new byte[81920];
        int read;
        while ((read = await body.ReadAsync(chunk, ct).ConfigureAwait(false)) > 0)
        {
            if (buffer.Length + read > max)
            {
                return null;
            }
            buffer.Write(chunk, 0, read);
        }
        return buffer.ToArray();
    }
}
