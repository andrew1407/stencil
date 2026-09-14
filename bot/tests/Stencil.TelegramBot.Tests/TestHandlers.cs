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
        PromptCancellations? cancellations = null,
        IReadOnlyList<LlmProfile>? profiles = null,
        ILogger<CommandHandlers>? logger = null,
        IScriptService? script = null)
    {
        UserWorkspace workspace = new(options);
        editing ??= new EditingService(cli, workspace, store);
        PromptService prompts = new(
            llm ?? new MockLlmClient(),
            editing,
            store,
            llmOptions ?? new LlmOptions(),
            new MockServerClientFactory(),
            profiles: profiles);
        return new CommandHandlers(
            editing,
            servers ?? new ThrowingServerService(),
            store,
            bot,
            options,
            new SyncRegistry(),
            new LayoutFetcher(options, isBlockedAddress: RemoteImageUrl.IsBlockedAddress),
            prompts,
            script ?? new ScriptService(cli, workspace, store, editing, prompts),
            new LlmAttachmentLoader(new MockImageDownscaler()),
            cancellations ?? new PromptCancellations(),
            logger ?? NullLogger<CommandHandlers>.Instance);
    }
}
