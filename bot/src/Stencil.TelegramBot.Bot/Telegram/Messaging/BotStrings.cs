using System.Text.Json;
using Telegram.Bot.Types.ReplyMarkups;

namespace Stencil.TelegramBot.Bot.Telegram.Messaging;

// The chat copy from the embedded Assets/botStrings.json; composition stays in code, only the
// wording lives there.
public static class BotStrings
{
    private const string _resourceName = "Stencil.TelegramBot.Bot.Assets.botStrings.json";

    private static readonly Lazy<Loaded> _asset = new(load);

    private sealed record Loaded(
        IReadOnlyDictionary<string, string> Replies,
        IReadOnlyDictionary<string, string> Tones,
        IReadOnlyDictionary<string, string> Marks,
        IReadOnlyDictionary<string, (string Label, string Token)> Buttons);

    public static string Reply(string key) =>
        _asset.Value.Replies.TryGetValue(key, out string? text)
            ? text
            : throw new InvalidOperationException($"botStrings.json has no reply '{key}'");

    public static string Reply(string key, params object?[] args) => string.Format(Reply(key), args);

    public static string Tone(string name) => _asset.Value.Tones[name];

    public static string Mark(string name) => _asset.Value.Marks[name];

    public static InlineKeyboardButton Button(string id)
    {
        (string label, string token) = _asset.Value.Buttons.TryGetValue(id, out var b)
            ? b
            : throw new InvalidOperationException($"botStrings.json has no button '{id}'");
        return InlineKeyboardButton.WithCallbackData(label, token);
    }

    private static Loaded load()
    {
        using Stream stream = typeof(BotStrings).Assembly.GetManifestResourceStream(_resourceName)
            ?? throw new InvalidOperationException($"embedded resource {_resourceName} is missing");
        using JsonDocument doc = JsonDocument.Parse(stream);
        Dictionary<string, (string, string)> buttons = new(StringComparer.Ordinal);
        foreach (JsonProperty entry in doc.RootElement.GetProperty("buttons").EnumerateObject())
        {
            buttons[entry.Name] = (entry.Value[0].GetString()!, entry.Value[1].GetString()!);
        }
        return new Loaded(strings(doc, "replies"), strings(doc, "tones"), strings(doc, "marks"), buttons);
    }

    private static Dictionary<string, string> strings(JsonDocument doc, string section)
    {
        Dictionary<string, string> map = new(StringComparer.Ordinal);
        foreach (JsonProperty entry in doc.RootElement.GetProperty(section).EnumerateObject())
        {
            map[entry.Name] = entry.Value.GetString()!;
        }
        return map;
    }
}
