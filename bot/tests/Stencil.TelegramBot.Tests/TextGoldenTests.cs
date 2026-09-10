using System.Reflection;
using System.Text;
using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Domain.Llm;
using Telegram.Bot.Types.ReplyMarkups;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// Byte-exact goldens for the bot's user-facing text: the <see cref="Replies"/> fixed strings,
/// every inline-keyboard label + callback token in <see cref="Keyboards"/>, and the
/// <see cref="BotCommandList"/> menu. The literals are slated to move into JSON assets, so
/// these pin the current bytes to prove the move is verbatim. Rewrite with
/// <c>BOT_UPDATE_GOLDENS=1 dotnet test</c>.
/// </summary>
public sealed class TextGoldenTests
{
    [Fact]
    public void RepliesFixedStringsMatchTheGolden() =>
        TextGolden.Check("replies.txt", BuildReplies());

    [Fact]
    public void KeyboardLabelsAndTokensMatchTheGolden() =>
        TextGolden.Check("keyboards.txt", BuildKeyboards());

    [Fact]
    public void BotCommandMenuMatchesTheGolden() =>
        TextGolden.Check("commands.txt", BuildCommands());

    /// <summary>
    /// Every no-argument public string builder on <see cref="Replies"/>, enumerated by
    /// reflection so a new one shows up here instead of going unpinned, plus the handful whose
    /// only argument is a flag.
    /// </summary>
    private static string BuildReplies()
    {
        StringBuilder sb = new();
        foreach (Replies.Tone tone in Enum.GetValues<Replies.Tone>().OrderBy(t => t.ToString(), StringComparer.Ordinal))
        {
            Section(sb, $"Glyph({tone})", Replies.Glyph(tone));
        }
        MethodInfo[] fixedStrings = typeof(Replies)
            .GetMethods(BindingFlags.Public | BindingFlags.Static)
            .Where(m => m.ReturnType == typeof(string) && m.GetParameters().Length == 0)
            .OrderBy(m => m.Name, StringComparer.Ordinal)
            .ToArray();
        Assert.NotEmpty(fixedStrings);
        foreach (MethodInfo m in fixedStrings)
        {
            Section(sb, $"{m.Name}()", (string)m.Invoke(null, null)!);
        }
        foreach (bool flag in new[] { false, true })
        {
            Section(sb, $"ChatHistoryCleared({Flag(flag)})", Replies.ChatHistoryCleared(flag));
            Section(sb, $"ChatSaveStatus({Flag(flag)})", Replies.ChatSaveStatus(flag));
        }
        Section(sb, "ChatRestored(1)", Replies.ChatRestored(1));
        Section(sb, "ChatRestored(3)", Replies.ChatRestored(3));
        Section(sb, "ConnectionsText([])", Replies.ConnectionsText([]));
        Section(sb, "ConnectionsText([], \"admin\")", Replies.ConnectionsText([], "admin"));
        Section(sb, "ProjectsText([])", Replies.ProjectsText([]));
        Section(sb, "ExpiryPrompt(0)", Replies.ExpiryPrompt(0));
        Section(sb, "ExpiryPrompt(1767225600000)", Replies.ExpiryPrompt(1767225600000));
        Section(sb, "DeleteConfirmPrompt(\"Poster\", null)", Replies.DeleteConfirmPrompt("Poster", null));
        Section(sb, "DeleteConfirmPrompt(\"Poster\", \"http://localhost:8090\")",
            Replies.DeleteConfirmPrompt("Poster", "http://localhost:8090"));
        Section(sb, "DesktopLink(\"Poster\", \"https://s/launch.html\", loopback: false)",
            Replies.DesktopLink("Poster", "https://s/launch.html", false));
        Section(sb, "DesktopLink(\"Poster\", \"http://localhost:8080/launch.html\", loopback: true)",
            Replies.DesktopLink("Poster", "http://localhost:8080/launch.html", true));
        return sb.ToString();
    }

    /// <summary>Every keyboard, row by row, as <c>label => callback token</c>.</summary>
    private static string BuildKeyboards()
    {
        LlmProfile[] profiles =
        [
            new() { Name = "local", Label = "Local (Ollama)", Options = new LlmOptions { Provider = "ollama", BaseUrl = "http://localhost:11434", Model = "llama3" } },
            new() { Name = "studio", Label = "LM Studio", Options = new LlmOptions { Provider = "openai-compat", BaseUrl = "http://localhost:1234/v1", Model = "gpt-oss" } },
        ];
        StringBuilder sb = new();
        Keyboard(sb, "AskCardKeyboard([\"Left\", \"Right\"], multi: false, picked: [], allowCustom: false)",
            Keyboards.AskCardKeyboard(["Left", "Right"], false, [], false));
        Keyboard(sb, "AskCardKeyboard([\"Left\", \"Right\"], multi: true, picked: [1], allowCustom: true)",
            Keyboards.AskCardKeyboard(["Left", "Right"], true, [1], true));
        Keyboard(sb, "ChatApiMenu(profiles, \"studio\")", Keyboards.ChatApiMenu(profiles, "studio"));
        Keyboard(sb, "ChatModeMenu(saveChats: false)", Keyboards.ChatModeMenu(false));
        Keyboard(sb, "ChatModeMenu(saveChats: true)", Keyboards.ChatModeMenu(true));
        Keyboard(sb, "ClearChatConfirmMenu()", Keyboards.ClearChatConfirmMenu());
        Keyboard(sb, "DeleteConfirmMenu()", Keyboards.DeleteConfirmMenu());
        Keyboard(sb, "DownloadSubmenu(hasEdits: false)", Keyboards.DownloadSubmenu(false));
        Keyboard(sb, "DownloadSubmenu(hasEdits: true)", Keyboards.DownloadSubmenu(true));
        Keyboard(sb, "DrawSubmenu()", Keyboards.DrawSubmenu());
        Keyboard(sb, "EditMenu(hasActiveProject: false)", Keyboards.EditMenu(false));
        Keyboard(sb, "EditMenu(hasActiveProject: true)", Keyboards.EditMenu(true));
        Keyboard(sb, "EditSubmenu()", Keyboards.EditSubmenu());
        Keyboard(sb, "ExpirationMenu()", Keyboards.ExpirationMenu());
        Keyboard(sb, "FilterSubmenu()", Keyboards.FilterSubmenu());
        Keyboard(sb, "MainMenu()", Keyboards.MainMenu());
        Keyboard(sb, "RetryPrompt()", Keyboards.RetryPrompt());
        Keyboard(sb, "StatusMenu(hasActiveProject: false)", Keyboards.StatusMenu(false));
        Keyboard(sb, "StatusMenu(hasActiveProject: true)", Keyboards.StatusMenu(true));
        Keyboard(sb, "StopPrompt()", Keyboards.StopPrompt());
        return sb.ToString();
    }

    private static string BuildCommands()
    {
        StringBuilder sb = new();
        foreach (var c in BotCommandList.All())
        {
            sb.Append('/').Append(c.Command).Append(" — ").Append(c.Description).Append('\n');
        }
        return sb.ToString();
    }

    private static string Flag(bool value) => value ? "true" : "false";

    private static void Section(StringBuilder sb, string header, string body) =>
        sb.Append("== ").Append(header).Append('\n').Append(body).Append("\n\n");

    private static void Keyboard(StringBuilder sb, string header, InlineKeyboardMarkup markup)
    {
        sb.Append("== ").Append(header).Append('\n');
        foreach (IEnumerable<InlineKeyboardButton> row in markup.InlineKeyboard)
        {
            sb.Append(string.Join("  |  ", row.Select(b => $"{b.Text} => {b.CallbackData}"))).Append('\n');
        }
        sb.Append('\n');
    }
}

/// <summary>
/// Byte-exact golden files under <c>bot/tests/Stencil.TelegramBot.Tests/Goldens/</c>, located
/// off the repo root like <see cref="SharedFixtures"/> does. <c>BOT_UPDATE_GOLDENS=1</c>
/// rewrites them instead of asserting.
/// </summary>
internal static class TextGolden
{
    public static void Check(string name, string actual)
    {
        string path = SharedFixtures.PathOf("bot", "tests", "Stencil.TelegramBot.Tests", "Goldens", name);
        if (Environment.GetEnvironmentVariable("BOT_UPDATE_GOLDENS") == "1")
        {
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllText(path, actual);
            return;
        }
        Assert.True(File.Exists(path), $"missing golden {path} — rerun with BOT_UPDATE_GOLDENS=1");
        string expected = File.ReadAllText(path);
        if (expected == actual)
        {
            return;
        }
        Assert.Fail($"golden {name} differs (BOT_UPDATE_GOLDENS=1 to rewrite):\n{Diff(expected, actual)}");
    }

    /// <summary>The first few differing lines, so a failure names the wording that moved.</summary>
    private static string Diff(string expected, string actual)
    {
        string[] want = expected.Split('\n');
        string[] got = actual.Split('\n');
        StringBuilder sb = new();
        int shown = 0;
        for (int i = 0; i < Math.Max(want.Length, got.Length) && shown < 12; i++)
        {
            string w = i < want.Length ? want[i] : "<eof>";
            string g = i < got.Length ? got[i] : "<eof>";
            if (w == g)
            {
                continue;
            }
            sb.Append($"  line {i + 1}:\n    want {w}\n    got  {g}\n");
            shown++;
        }
        if (shown == 0)
        {
            sb.Append($"  identical line-wise; lengths {expected.Length} vs {actual.Length}\n");
        }
        return sb.ToString();
    }
}
