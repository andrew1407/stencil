using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Links;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Infrastructure.Workspace;
using Stencil.TelegramBot.Tests.Doubles;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The one place handler fixtures build their <see cref="CommandHandlers"/>: the full
/// construction (a real <see cref="EditingService"/> + <see cref="PromptService"/> over the
/// mocks) with optional overrides, so adding a dependency means touching this factory instead
/// of every fixture.
/// </summary>
internal static class TestHandlers
{
    public static CommandHandlers Create(
        BotOptions options,
        InMemorySessionStore store,
        MockStencilCli cli,
        MockBotClient bot,
        MockLlmClient? llm = null,
        IServerService? servers = null,
        EditingService? editing = null,
        LlmOptions? llmOptions = null,
        bool openLlmAllowlist = true,
        PromptCancellations? cancellations = null,
        IReadOnlyList<LlmProfile>? profiles = null,
        ILogger<CommandHandlers>? logger = null)
    {
        editing ??= new EditingService(cli, new UserWorkspace(options), store);
        // The assistant is allowlisted per Telegram user id and fails closed when the list is
        // empty (BotOptions.LlmAllowedUsers). Fixtures exercising the handlers themselves opt
        // out of the gate with an allow-everyone set, since each uses its own user id. The gate
        // itself is covered by AssistantAllowlistTests, which passes openLlmAllowlist:false to
        // get the real options through untouched.
        if (openLlmAllowlist)
        {
            options = options with { LlmAllowedUsers = AnyUser.Instance };
        }
        return new CommandHandlers(
            editing,
            servers ?? new ThrowingServerService(),
            store,
            bot,
            options,
            new SyncRegistry(),
            new LayoutFetcher(options, isBlockedAddress: RemoteImageUrl.IsBlockedAddress),
            new PromptService(
                llm ?? new MockLlmClient(),
                editing,
                store,
                llmOptions ?? new LlmOptions(),
                new MockServerClientFactory(),
                profiles: profiles),
            new LlmAttachmentLoader(new MockImageDownscaler()),
            cancellations ?? new PromptCancellations(),
            logger ?? NullLogger<CommandHandlers>.Instance);
    }
}
