using System.Text.Json;

namespace Stencil.TelegramBot.Domain.Serialization;

/// <summary>
/// Tolerant <see cref="JsonElement"/> readers shared by the HTTP adapters (the server REST
/// client and the LLM client): missing or wrongly-typed properties read as <c>""</c> / 0
/// instead of throwing, and the common error-body shapes reduce to one detail string.
/// </summary>
public static class JsonRead
{
    /// <summary>Read a string property, or "" when missing / not a string.</summary>
    public static string ReadString(JsonElement element, string name) =>
        element.TryGetProperty(name, out JsonElement value) && value.ValueKind == JsonValueKind.String
            ? value.GetString() ?? ""
            : "";

    /// <summary>Read an integer property, or 0 when missing / not a number.</summary>
    public static int ReadInt(JsonElement element, string name) =>
        element.TryGetProperty(name, out JsonElement value) && value.ValueKind == JsonValueKind.Number
            ? value.GetInt32()
            : 0;

    /// <summary>
    /// The failure detail carried by an error body: the collaboration server's
    /// <c>{message}</c> shape, or the OpenAI/ollama <c>{error:{message}}</c> /
    /// <c>{error:"…"}</c> shapes. "" when the body carries none.
    /// </summary>
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
