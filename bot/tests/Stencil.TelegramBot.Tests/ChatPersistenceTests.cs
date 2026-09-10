using System.Text;
using System.Text.Json;
using Microsoft.Extensions.Logging.Abstractions;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Infrastructure.Workspace;
using Stencil.TelegramBot.Tests.Doubles;
using Telegram.Bot.Requests;
using Telegram.Bot.Types;
using Telegram.Bot.Types.ReplyMarkups;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// Opt-in per-project chat persistence (contract §12) through the real
/// <see cref="UpdateRouter"/> + <see cref="CommandHandlers"/> + <see cref="PromptService"/> +
/// <see cref="ServerService"/> over in-memory mocks: the <c>/chat save on|off</c> toggle (and
/// its 💾 button), the after-turn push of the §12.1 document to the server project's <c>chat</c>
/// kind, restore-on-fetch, delete-on-clear, and the default-off guarantee that no server file
/// call ever happens without the opt-in.
/// </summary>
public sealed class ChatPersistenceTests : IDisposable
{
    private const long UserId = 95;
    private const long ChatId = 96;
    private const string Server = "http://srv:8090";
    private const string ProjectId = "p_1";

    private readonly string _dataDir;
    private readonly MockStencilCli _cli = new();
    private readonly MockBotClient _bot = new();
    private readonly MockLlmClient _llm = new();
    private readonly InMemorySessionStore _store = new();
    private readonly MockServerClientFactory _factory = new();
    private readonly CommandHandlers _handlers;
    private readonly CallbackAction _callbacks;
    private readonly UpdateRouter _router;

    public ChatPersistenceTests()
    {
        _dataDir = Path.Combine(Path.GetTempPath(), "stencil-bot-chatsave-" + Guid.NewGuid().ToString("N"));
        BotOptions options = new() { DataDir = _dataDir, AllowedUsers = AnyUser.Instance };
        EditingService editing = new(_cli, new UserWorkspace(options), _store);
        ServerService servers = new(_factory, _store, editing);
        _handlers = TestHandlers.Create(options, _store, _cli, _bot, _llm, servers: servers, editing: editing);
        _callbacks = new CallbackAction(_handlers, _bot, _store);
        _router = new UpdateRouter(
            _handlers,
            _callbacks,
            editing,
            _store,
            _bot,
            new UserGate(),
            options,
            NullLogger<UpdateRouter>.Instance);
    }

    public void Dispose()
    {
        try { Directory.Delete(_dataDir, recursive: true); } catch { /* best effort */ }
    }

    private MockStencilServerClient ServerClient => _factory.ClientFor(Server);

    private Task Send(string text) =>
        _router.HandleMessageAsync(
            new Message
            {
                Chat = new Chat { Id = ChatId },
                From = new User { Id = UserId },
                Text = text,
            },
            CancellationToken.None);

    private Task Tap(string data) =>
        _callbacks.HandleAsync(
            new CallbackQuery
            {
                Id = "cb",
                From = new User { Id = UserId },
                Message = new Message { Chat = new Chat { Id = ChatId } },
                Data = data,
            },
            CancellationToken.None);

    private IEnumerable<SendMessageRequest> Messages => _bot.Requests.OfType<SendMessageRequest>();

    /// <summary>Seed a server project and open it, bypassing DNS-touching /connect validation.</summary>
    private async Task OpenProjectAsync()
    {
        ServerClient.Seed(new ProjectRecord
        {
            Id = ProjectId,
            Name = "Poster",
            HasImage = true,
            ImageW = 320,
            ImageH = 240,
            Version = 3,
        });
        UserSession session = await _store.GetAsync(UserId);
        await _store.SaveAsync(session with
        {
            Connections = [new ServerConnectionInfo { Url = Server, Token = "tok", VerifyTls = true }],
        });
        await Send("/fetch Poster");
    }

    private IEnumerable<(string Id, string Kind, byte[] Data, string Ext, int W, int H)> ChatPuts =>
        ServerClient.Puts.Where(p => p.Kind == ProjectFileKind.Chat);

    [Fact]
    public async Task ChatSaveOnPersistsTheFlagAndOffTurnsItBack()
    {
        await Send("/chat save on");
        Assert.True((await _store.GetAsync(UserId)).SaveChats);
        Assert.Contains("Chat saving on", Messages.Last().Text);
        // §12.2: the bot's only store IS the server project, so turning this on must
        // say that anyone the project is shared with can read the conversation.
        Assert.Contains("shared with", Messages.Last().Text);

        await Send("/chat save");
        Assert.Contains("Chat saving is on", Messages.Last().Text);
        Assert.Contains("shared with", Messages.Last().Text);

        await Send("/chat save off");
        Assert.False((await _store.GetAsync(UserId)).SaveChats);
        Assert.Contains("Chat saving off", Messages.Last().Text);
    }

    /// <summary>
    /// §12.2 requires the sharing consequence to be stated wherever the toggle is offered
    /// — not only on the confirmation once it is already on. A transcript records what the
    /// user asked for in their own words, and the bot's only store IS the server project,
    /// so it inherits that project's access. The two affordances the confirmation test does
    /// not reach: the status a user reads BEFORE opting in (the moment the disclosure has
    /// to land), and the 💾 button, which must not be a quieter path to the same decision.
    /// </summary>
    [Fact]
    public async Task EveryChatSaveAffordanceSaysWhoCanReadTheTranscript()
    {
        await Send("/chat save");
        Assert.False((await _store.GetAsync(UserId)).SaveChats); // still the default
        Assert.Contains("Chat saving is off", Messages.Last().Text);
        Assert.Contains("shared with", Messages.Last().Text);

        await Tap("chat:save-on");
        Assert.True((await _store.GetAsync(UserId)).SaveChats);
        Assert.Contains("shared with", Messages.Last().Text);
    }

    [Fact]
    public async Task TheSaveButtonTogglesTheFlagAndTheMenuShowsTheState()
    {
        await Tap("chat:save-on");
        Assert.True((await _store.GetAsync(UserId)).SaveChats);

        // Entering chat mode now shows the toggle reflecting the ON state (tap turns it off).
        await Send("/chat");
        SendMessageRequest confirm = Messages.Last();
        InlineKeyboardMarkup markup = Assert.IsType<InlineKeyboardMarkup>(confirm.ReplyMarkup);
        InlineKeyboardButton save = Assert.Single(
            markup.InlineKeyboard.SelectMany(r => r), b => b.CallbackData!.StartsWith("chat:save-"));
        Assert.Equal("💾 Save chats: on", save.Text);
        Assert.Equal("chat:save-off", save.CallbackData);

        await Tap("chat:save-off");
        Assert.False((await _store.GetAsync(UserId)).SaveChats);
    }

    [Fact]
    public async Task APromptTurnPushesTheDisplayedReplyDocumentToTheChatKind()
    {
        await OpenProjectAsync();
        await Send("/chat save on");
        _llm.CannedReplies.Enqueue(new LlmReply(
            """{"version":1,"reply":"Made it black & white.","actions":[{"op":"filter","mode":"bw"}]}"""));

        await Send("/prompt make it bw");

        var put = Assert.Single(ChatPuts);
        Assert.Equal(ProjectId, put.Id);
        Assert.Equal("json", put.Ext);
        using JsonDocument doc = JsonDocument.Parse(Encoding.UTF8.GetString(put.Data));
        Assert.Equal(1, doc.RootElement.GetProperty("version").GetInt32());
        Assert.True(doc.RootElement.GetProperty("savedAt").GetInt64() > 0);
        JsonElement[] messages = doc.RootElement.GetProperty("messages").EnumerateArray().ToArray();
        Assert.Equal(2, messages.Length);
        Assert.Equal("user", messages[0].GetProperty("role").GetString());
        Assert.Equal("make it bw", messages[0].GetProperty("text").GetString());
        Assert.Equal("assistant", messages[1].GetProperty("role").GetString());
        // The DISPLAYED reply is persisted (contract §12.1), never the raw JSON plan…
        Assert.Equal("Made it black & white.", messages[1].GetProperty("text").GetString());
        // …and the document is text-only: the vision attachment never reaches the server.
        Assert.DoesNotContain("images", Encoding.UTF8.GetString(put.Data));
        // §9: the chat upload is filestore-only — the project version was not bumped.
        Assert.Equal(3, (await ServerClient.GetProjectAsync(ProjectId)).Project.Version);
    }

    [Fact]
    public async Task ChatClearWithSavingOnDeletesTheServerCopy()
    {
        await OpenProjectAsync();
        await Send("/chat save on");
        await Send("/prompt hello there");
        Assert.Single(ChatPuts);

        await Send("/chat clear");

        Assert.Contains((ProjectId, ProjectFileKind.Chat), ServerClient.FileDeletes);
        Assert.Contains("Conversation cleared", Messages.Last().Text);
    }

    [Fact]
    public async Task FetchWithSavingOnSeedsTheHistoryFromTheStoredDocument()
    {
        ServerClient.Files[(ProjectId, ProjectFileKind.Chat)] = Encoding.UTF8.GetBytes(
            """
            {"version":1,"savedAt":1753900000000,"messages":[
              {"role":"user","text":"crop 10% off the left"},
              {"role":"assistant","text":"Done — anything else?"}]}
            """);
        await Send("/chat save on");

        await OpenProjectAsync();

        Assert.Contains("Restored 2 saved chat messages", Messages.Last().Text);

        await Send("/prompt what did we do so far?");

        LlmChatRequest request = _llm.Requests[^1];
        Assert.Equal(3, request.Messages.Count);
        Assert.Equal(LlmMessage.RoleUser, request.Messages[0].Role);
        Assert.Equal("crop 10% off the left", request.Messages[0].Text);
        Assert.Equal(LlmMessage.RoleAssistant, request.Messages[1].Role);
        Assert.Equal("Done — anything else?", request.Messages[1].Text);
        Assert.Equal("what did we do so far?", request.Messages[2].Text);
    }

    [Fact]
    public async Task FetchWithoutAStoredChatRestoresNothingAndStillLoads()
    {
        await Send("/chat save on");

        await OpenProjectAsync();

        Assert.Contains("Loaded project 'Poster'", Messages.Last().Text);
        Assert.DoesNotContain("Restored", Messages.Last().Text);
    }

    [Fact]
    public async Task WithSavingOffNoServerFileCallEverHappens()
    {
        ServerClient.Files[(ProjectId, ProjectFileKind.Chat)] = Encoding.UTF8.GetBytes(
            """{"version":1,"messages":[{"role":"user","text":"old"}]}""");

        await OpenProjectAsync();               // SaveChats defaults off
        Assert.DoesNotContain("Restored", Messages.Last().Text);

        await Send("/prompt make it nicer");
        await Send("/chat clear");

        Assert.Empty(ChatPuts);
        Assert.Empty(ServerClient.FileDeletes);
        // The stored chat was not loaded either: the next turn starts fresh.
        await Send("/prompt hi");
        Assert.Single(_llm.Requests[^1].Messages, m => m.Role == LlmMessage.RoleUser && m.Text == "hi");
    }

    [Fact]
    public async Task ASaveFailureWarnsOnceKeepsTheReplyAndRearmsOnSuccess()
    {
        await OpenProjectAsync();
        await Send("/chat save on");
        ServerClient.ThrowOnPutKind = ProjectFileKind.Chat;

        await Send("/prompt one");
        await Send("/prompt two");

        // Both replies were delivered (the default plan answers "ok")…
        Assert.Equal(2, Messages.Count(m => m.Text == "ok"));
        // …and the soft failure was surfaced exactly once, not per turn.
        Assert.Single(Messages, m => m.Text.Contains("Couldn't store the conversation"));

        ServerClient.ThrowOnPutKind = null;
        await Send("/prompt three");

        Assert.Single(ChatPuts);
        Assert.Single(Messages, m => m.Text.Contains("Couldn't store the conversation"));
    }
}
