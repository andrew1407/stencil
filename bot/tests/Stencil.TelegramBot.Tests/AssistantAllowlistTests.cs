using Microsoft.Extensions.Logging;
using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Tests.Doubles;
using Telegram.Bot.Requests;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The assistant's per-user allowlist (<c>STENCIL_BOT_ALLOWED_USERS</c>): every LLM turn spends
/// the operator's single API key and any Telegram user can message the bot, so <c>/prompt</c> and
/// <c>/chat</c> are opt-in per user id and off until the list is configured. The refusal the user
/// reads is one plain sentence; the env var, the procedure and the caller's id are operator
/// detail and go to the log instead.
/// </summary>
public sealed class AssistantAllowlistTests : IDisposable
{
    private const long Allowed = 55;
    private const long Stranger = 999;
    private const long ChatId = 66;

    private readonly string _dataDir;
    private readonly MockStencilCli _cli = new();
    private readonly MockBotClient _bot = new();
    private readonly MockLlmClient _llm = new();
    private readonly MockLogger<CommandHandlers> _log = new();
    private readonly InMemorySessionStore _store = new();

    public AssistantAllowlistTests()
    {
        _dataDir = Path.Combine(Path.GetTempPath(), "stencil-bot-allowlist-" + Guid.NewGuid().ToString("N"));
    }

    public void Dispose()
    {
        try { Directory.Delete(_dataDir, recursive: true); } catch { /* best effort */ }
    }

    private CommandHandlers HandlersFor(params long[] allowed) =>
        TestHandlers.Create(
            new BotOptions { DataDir = _dataDir, LlmAllowedUsers = new HashSet<long>(allowed) },
            _store, _cli, _bot, _llm, openLlmAllowlist: false, logger: _log);

    private static string LastText(MockBotClient bot) =>
        bot.Requests.OfType<SendMessageRequest>().Last().Text;

    // Nothing operator-shaped reaches the chat: no env var, no configuration recipe, no id.
    private void AssertNoOperatorDetail(long userId)
    {
        string text = LastText(_bot);
        Assert.DoesNotContain("STENCIL_", text);
        Assert.DoesNotContain("STENCIL_BOT_ALLOWED_USERS", text);
        Assert.DoesNotContain(userId.ToString(), text);
        Assert.DoesNotContain("operator", text, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task AnEmptyListTurnsTheAssistantOffForEveryone()
    {
        // A HashSet with no entries — NOT the tests' allow-everyone default.
        CommandHandlers handlers = HandlersFor();

        await handlers.DispatchAsync(Allowed, ChatId, CommandParser.Parse("/prompt make it sepia"), CancellationToken.None);

        Assert.Empty(_llm.Requests);
        // Refusals wear the error glyph like every other "this didn't happen" reply.
        Assert.Equal("🔴 The AI assistant isn't enabled on this bot.", LastText(_bot));
        AssertNoOperatorDetail(Allowed);
    }

    [Fact]
    public async Task AUserOffTheListIsRefusedWithoutOperatorDetail()
    {
        CommandHandlers handlers = HandlersFor(Allowed);

        await handlers.DispatchAsync(Stranger, ChatId, CommandParser.Parse("/prompt make it sepia"), CancellationToken.None);

        Assert.Empty(_llm.Requests);
        Assert.Equal("🔴 The AI assistant isn't enabled for your account.", LastText(_bot));
        AssertNoOperatorDetail(Stranger);
    }

    // The operator still learns how to open the gate — from the log, not the chat.
    [Fact]
    public async Task TheOperatorHintGoesToTheLog()
    {
        CommandHandlers handlers = HandlersFor(Allowed);

        await handlers.DispatchAsync(Stranger, ChatId, CommandParser.Parse("/prompt make it sepia"), CancellationToken.None);

        (LogLevel Level, string Message) entry = Assert.Single(_log.Entries);
        Assert.Equal(LogLevel.Warning, entry.Level);
        Assert.Contains("STENCIL_BOT_ALLOWED_USERS", entry.Message);
        Assert.Contains(Stranger.ToString(), entry.Message);
    }

    // Rate-limited per user id: a stranger hammering /prompt writes one line, not one per try.
    [Fact]
    public async Task TheOperatorHintIsLoggedOncePerUser()
    {
        CommandHandlers handlers = HandlersFor(Allowed);

        for (int i = 0; i < 3; i++)
        {
            await handlers.DispatchAsync(Stranger, ChatId, CommandParser.Parse("/prompt make it sepia"), CancellationToken.None);
        }
        await handlers.DispatchAsync(12345, ChatId, CommandParser.Parse("/prompt make it sepia"), CancellationToken.None);

        Assert.Equal(2, _log.Entries.Count);
        Assert.Contains(_log.Messages, m => m.Contains(Stranger.ToString()));
        Assert.Contains(_log.Messages, m => m.Contains("12345"));
    }

    [Fact]
    public async Task ChatModeIsGatedToo()
    {
        CommandHandlers handlers = HandlersFor(Allowed);

        await handlers.DispatchAsync(Stranger, ChatId, CommandParser.Parse("/chat on"), CancellationToken.None);

        Assert.False((await _store.GetAsync(Stranger, CancellationToken.None)).ChatMode);
        Assert.Equal("🔴 The AI assistant isn't enabled for your account.", LastText(_bot));
        AssertNoOperatorDetail(Stranger);
    }

    // Never gated: someone who used the assistant earlier must still be able to delete it.
    [Fact]
    public async Task ClearingAChatTranscriptIsNeverGated()
    {
        CommandHandlers handlers = HandlersFor(Allowed);

        await handlers.DispatchAsync(Stranger, ChatId, CommandParser.Parse("/chat clear"), CancellationToken.None);

        Assert.DoesNotContain("STENCIL_BOT_ALLOWED_USERS", LastText(_bot));
        Assert.Empty(_log.Entries);
    }

    [Fact]
    public async Task AListedUserGoesStraightThrough()
    {
        CommandHandlers handlers = HandlersFor(Allowed, Stranger);

        await handlers.DispatchAsync(Allowed, ChatId, CommandParser.Parse("/prompt make it sepia"), CancellationToken.None);

        Assert.Single(_llm.Requests);
        Assert.Empty(_log.Entries);
    }
}
