using System.Text.Json;
using Stencil.TelegramBot.Domain.Serialization;

namespace Stencil.TelegramBot.Domain.Layout;

public static class StencilLayoutParser
{
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
