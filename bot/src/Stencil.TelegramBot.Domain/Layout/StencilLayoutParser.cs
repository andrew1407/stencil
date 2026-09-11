using System.Text.Json;
using Stencil.TelegramBot.Domain.Serialization;

namespace Stencil.TelegramBot.Domain.Layout;

// Shared by the .json document upload and /layout so both validate identically.
public static class StencilLayoutParser
{
    // Null on malformed JSON / a non-layout shape.
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
