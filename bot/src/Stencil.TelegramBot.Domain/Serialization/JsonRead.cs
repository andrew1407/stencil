using System.Text.Json;

namespace Stencil.TelegramBot.Domain.Serialization;

// Tolerant JsonElement readers for the HTTP adapters: a missing or wrongly-typed property
// reads as "" / 0 rather than throwing.
public static class JsonRead
{
    public static string ReadString(JsonElement element, string name) =>
        element.TryGetProperty(name, out JsonElement value) && value.ValueKind == JsonValueKind.String
            ? value.GetString() ?? ""
            : "";

    public static int ReadInt(JsonElement element, string name) =>
        element.TryGetProperty(name, out JsonElement value) && value.ValueKind == JsonValueKind.Number
            ? value.GetInt32()
            : 0;

    // The server's {message} shape, or the OpenAI/ollama {error:{message}} / {error:"…"} ones.
    public static string ErrorDetail(JsonElement root)
    {
        if (root.ValueKind != JsonValueKind.Object)
        {
            return "";
        }
        string detail = ReadString(root, "message");
        if (detail.Length == 0 && root.TryGetProperty("error", out JsonElement error))
        {
            detail = error.ValueKind switch
            {
                JsonValueKind.String => error.GetString() ?? "",
                JsonValueKind.Object => ReadString(error, "message"),
                _ => "",
            };
        }
        return detail;
    }
}
