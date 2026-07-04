using System.Text.Json;
using Stencil.TelegramBot.Domain.Serialization;

namespace Stencil.TelegramBot.Domain.Layout;

/// <summary>
/// Parse raw bytes into a <see cref="StencilLayout"/> via the shared JSON conventions.
/// Shared by the <c>.json</c> document upload (UpdateRouter) and the <c>/layout</c> command
/// so both apply exactly the same validation.
/// </summary>
public static class StencilLayoutParser
{
    /// <summary>Parse layout bytes; null on malformed JSON / a non-layout shape.</summary>
    public static StencilLayout? Parse(byte[] bytes)
    {
        try
        {
            using JsonDocument document = JsonDocument.Parse(bytes);
            return StencilJson.FromElement<StencilLayout>(document.RootElement);
        }
        catch (JsonException)
        {
            return null;
        }
    }
}
