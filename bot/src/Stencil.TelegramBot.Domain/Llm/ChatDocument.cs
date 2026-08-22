using System.Text.Json;
using Stencil.TelegramBot.Domain.Serialization;

namespace Stencil.TelegramBot.Domain.Llm;

/// <summary>One persisted chat message: role (user|assistant) and text only — never images.</summary>
public sealed record ChatDocumentMessage(string Role, string Text);

/// <summary>
/// The contract's persisted-chat document (<c>llm-contract.md</c> §12.1) — the one shape
/// every surface reads and writes when a conversation is saved with a project. Text-only,
/// ≤ 32 messages, tolerant on read: a wrong <c>version</c> or shape means "no chat" (never an
/// error), unknown fields and stray <c>images</c> are ignored.
/// </summary>
/// <remarks>
/// For the bot the store is the active server project's <c>chat</c> file kind (§12.3 — no
/// local store). Assistant <c>Text</c> must be the DISPLAYED reply, never the raw JSON plan:
/// the extraction is the caller's job (see <c>PromptService.BuildChatDocument</c>), and
/// <see cref="DisplayText"/> is the backstop that refuses one on both sides.
/// </remarks>
public sealed record ChatDocument
{
    /// <summary>The §12.1 message bound (the §7 history bound): the most recent 32 survive.</summary>
    public const int MaxMessages = 32;

    /// <summary>
    /// §7's auto-continuation note: the internal sentence <c>PromptService</c> appends to the
    /// RESTATED request after a plan made a new picture. It lives beside the §12 rules so the
    /// one place that writes it and the one that must never persist it agree by construction.
    /// </summary>
    public const string ContinuationNote =
        "[The working image is now the picture those actions just made — continue with it, using its real pixel size.]";

    private const string ContinuationOpen = "[The working image is now";

    /// <summary>
    /// The §12.1 text to persist/restore for one turn, or null when the turn is dropped. The
    /// document is SHARED across surfaces and a restored transcript must read as a conversation,
    /// so machinery never enters it. Applied on BOTH sides (<see cref="Build"/> and
    /// <see cref="TryParse"/>), which is what keeps another surface's — or an older build's —
    /// internals from being replayed as the user's own words:
    /// <list type="bullet">
    /// <item>§7's continuation note: stripped off the restated request it trails, or the whole
    /// turn dropped when it stands alone (any bracketed wording).</item>
    /// <item>An assistant turn that is a raw op-plan: §7 permits that on the WIRE, §12.1 does
    /// not. Assistant turns only — a user may paste JSON and see it again.</item>
    /// </list>
    /// </summary>
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

    /// <summary>A raw op-plan: a JSON object/array carrying "version" plus one of the plan's fields.</summary>
    private static bool LooksLikeRawPlan(string t) =>
        t[0] is '{' or '['
        && HasJsonKey(t, "version")
        && (HasJsonKey(t, "actions") || HasJsonKey(t, "reply")
            || HasJsonKey(t, "variants") || HasJsonKey(t, "ask"));

    /// <summary><c>"key"</c> followed by optional whitespace and a colon, anywhere in the text.</summary>
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

    /// <summary>Epoch ms when the document was written — informational only.</summary>
    public long SavedAt { get; init; }

    public IReadOnlyList<ChatDocumentMessage> Messages { get; init; } = [];

    /// <summary>
    /// Build a document from conversation messages: text-only (any images are stripped by
    /// construction), unknown roles dropped, §7 machinery refused by <see cref="DisplayText"/>,
    /// trimmed to the most recent <see cref="MaxMessages"/> of what survives.
    /// </summary>
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

    /// <summary>The camelCase §12.1 JSON (<c>{"version":1,"savedAt":…,"messages":[…]}</c>).</summary>
    public string ToJson() => StencilJson.Serialize(this);

    /// <summary>
    /// Tolerant §12.1 reader: null (treat as "no chat") for malformed JSON, a non-object, or a
    /// <c>version</c> other than 1; messages with a non-user/assistant role or without a string
    /// <c>text</c> are dropped; stray <c>images</c> and unknown fields are ignored; anything
    /// beyond <see cref="MaxMessages"/> is truncated to the most recent.
    /// </summary>
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
                    // §12.1: internal text from another surface (or an older build) never
                    // returns to the conversation as if the user wrote or saw it.
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
