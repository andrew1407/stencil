using System.Text.Json;
using Stencil.TelegramBot.Domain.Serialization;

namespace Stencil.TelegramBot.Domain.Llm;

public sealed record ChatDocumentMessage(string Role, string Text);

// The §12.1 persisted-chat document, text-only and tolerant on read: a wrong version or shape means
// "no chat", never an error. The bot's store is the server project's chat file (§12.3).
public sealed record ChatDocument
{
    // §12.1 (and the §7 history bound): the most recent 32 survive.
    public const int MaxMessages = 32;

    // §7's auto-continuation note, appended to the RESTATED request after a plan made a new
    // picture.
    public const string ContinuationNote =
        "[The working image is now the picture those actions just made — continue with it, using its real pixel size.]";

    private const string ContinuationOpen = "[The working image is now";

    // Applied on BOTH Build and TryParse so another surface's internals never replay as the user's
    // words: §7's continuation note is stripped, a raw op-plan is refused (assistant turns only).
    public static string? DisplayText(string role, string? text)
    {
        string t = (text ?? string.Empty).Trim();
        if (t.EndsWith(']'))
        {
            int at = t.LastIndexOf(ContinuationOpen, StringComparison.Ordinal);
            if (at >= 0)
            {
                t = t[..at].TrimEnd();
            }
        }
        if (t.Length == 0)
        {
            return null;
        }
        return role == LlmMessage.RoleAssistant && LooksLikeRawPlan(t) ? null : t;
    }

    private static bool LooksLikeRawPlan(string t) =>
        t[0] is '{' or '['
        && HasJsonKey(t, "version")
        && (HasJsonKey(t, "actions") || HasJsonKey(t, "reply")
            || HasJsonKey(t, "variants") || HasJsonKey(t, "ask"));

    private static bool HasJsonKey(string t, string key)
    {
        string quoted = $"\"{key}\"";
        for (int i = t.IndexOf(quoted, StringComparison.Ordinal); i >= 0;
             i = t.IndexOf(quoted, i + quoted.Length, StringComparison.Ordinal))
        {
            int j = i + quoted.Length;
            while (j < t.Length && char.IsWhiteSpace(t[j]))
            {
                j++;
            }
            if (j < t.Length && t[j] == ':')
            {
                return true;
            }
        }
        return false;
    }

    public int Version { get; init; } = 1;

    // Epoch ms; informational only.
    public long SavedAt { get; init; }

    public IReadOnlyList<ChatDocumentMessage> Messages { get; init; } = [];

    public static ChatDocument Build(IEnumerable<LlmMessage> messages, long savedAtMs)
    {
        List<ChatDocumentMessage> kept = new();
        foreach (LlmMessage message in messages)
        {
            if (message.Role is LlmMessage.RoleUser or LlmMessage.RoleAssistant
                && DisplayText(message.Role, message.Text) is string shown)
            {
                kept.Add(new ChatDocumentMessage(message.Role, shown));
            }
        }
        if (kept.Count > MaxMessages)
        {
            kept.RemoveRange(0, kept.Count - MaxMessages);
        }
        return new ChatDocument { SavedAt = savedAtMs, Messages = kept };
    }

    public string ToJson() => StencilJson.Serialize(this);

    // Null = "no chat" (malformed, non-object, version != 1); bad messages and unknown fields are
    // dropped.
    public static ChatDocument? TryParse(string? json)
    {
        if (string.IsNullOrWhiteSpace(json))
        {
            return null;
        }
        try
        {
            using JsonDocument doc = JsonDocument.Parse(json);
            JsonElement root = doc.RootElement;
            if (root.ValueKind != JsonValueKind.Object
                || !root.TryGetProperty("version", out JsonElement version)
                || version.ValueKind != JsonValueKind.Number
                || !version.TryGetInt32(out int v)
                || v != 1)
            {
                return null;
            }
            long savedAt = root.TryGetProperty("savedAt", out JsonElement at)
                && at.ValueKind == JsonValueKind.Number
                && at.TryGetInt64(out long ms)
                    ? ms
                    : 0;
            List<ChatDocumentMessage> messages = new();
            if (root.TryGetProperty("messages", out JsonElement list) && list.ValueKind == JsonValueKind.Array)
            {
                foreach (JsonElement item in list.EnumerateArray())
                {
                    if (item.ValueKind != JsonValueKind.Object)
                    {
                        continue;
                    }
                    string? role = item.TryGetProperty("role", out JsonElement r) && r.ValueKind == JsonValueKind.String
                        ? r.GetString()
                        : null;
                    string? text = item.TryGetProperty("text", out JsonElement t) && t.ValueKind == JsonValueKind.String
                        ? t.GetString()
                        : null;
                    if (role is not (LlmMessage.RoleUser or LlmMessage.RoleAssistant) || text is null)
                    {
                        continue;
                    }
                    if (DisplayText(role, text) is not string shown)
                    {
                        continue;
                    }
                    messages.Add(new ChatDocumentMessage(role, shown));
                }
            }
            if (messages.Count > MaxMessages)
            {
                messages.RemoveRange(0, messages.Count - MaxMessages);
            }
            return new ChatDocument { SavedAt = savedAt, Messages = messages };
        }
        catch (JsonException)
        {
            return null;
        }
    }
}
