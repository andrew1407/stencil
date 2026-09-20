using System.Collections.Concurrent;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Llm;

public sealed record PromptRender(string Label, RenderResult Result);

// The same bytes/name/caption /json and /project send; one document per action.
public sealed record PromptExport(string FileName, byte[] Bytes, string Caption);

// Applied is false on every refusal; Mutated is about pixels, so a save-only plan is applied without
// mutating. On Mutated the caller renders the result, not this record.
public sealed record PromptOutcome(
    string Reply,
    IReadOnlyList<string> Warnings,
    IReadOnlyList<PromptRender> Renders,
    AskCard? Ask = null,
    bool Mutated = false,
    IReadOnlyList<PromptExport>? Exports = null,
    bool ClearChatRequested = false,
    bool Applied = false)
{
    public IReadOnlyList<PromptExport> Exports { get; init; } = Exports ?? [];
}

// The /prompt engine: builds each turn per llm-contract.md and folds every action onto the same
// IEditingService methods the slash commands use. History holds base64 images, so it stays out of the session JSON.
public sealed partial class PromptService
{
    // The contract's §7 history bound.
    public const int MAX_HISTORY_MESSAGES = 32;

    private const int _maxLabelChars = 40;

    // Beyond it the least-recently-active conversation is forgotten.
    public const int MAX_TRACKED_USERS = 256;

    private sealed class UserHistory
    {
        public List<LlmMessage> Messages { get; } = new();
        public long Touched;
    }

    private readonly ILlmClient _llm;
    private readonly IEditingService _editing;
    private readonly ISessionStore _store;
    private readonly LlmOptions _options;
    private readonly IStencilServerClientFactory _servers;
    private readonly ConcurrentDictionary<long, UserHistory> _history = new();
    private long _clock;
    // Re-encodes the working image for the §7 auto-continuation; null = no continuation.
    private readonly LlmAttachmentLoader? _attachments;
    // The §2.1 `save` path: the user's active server project. Null = this surface can't save.
    private readonly IServerService? _projects;
    // The selectable chat APIs (/chatapi). Empty = every turn uses _options.
    private readonly IReadOnlyList<LlmProfile> _profiles;
    // The process-wide in-flight cap. Null (no DI registration) = unlimited.
    private readonly LlmGate _gate;

    public PromptService(
        ILlmClient llm,
        IEditingService editing,
        ISessionStore store,
        LlmOptions options,
        IStencilServerClientFactory servers,
        LlmAttachmentLoader? attachments = null,
        IServerService? projects = null,
        IReadOnlyList<LlmProfile>? profiles = null,
        LlmGate? gate = null)
    {
        _llm = llm;
        _editing = editing;
        _store = store;
        _options = options;
        _servers = servers;
        _attachments = attachments;
        _projects = projects;
        _profiles = profiles ?? [];
        _gate = gate ?? new LlmGate(0);
    }

    // A /chatapi profile since removed falls back to the operator's own rather than failing the
    // turn.
    private LlmOptions optionsFor(UserSession session) =>
        session.LlmProfile is not string name
            ? _options
            : _profiles.FirstOrDefault(p => string.Equals(p.Name, name, StringComparison.OrdinalIgnoreCase))?.Options
                ?? _options;

    public const string BUSY_REPLY = "The assistant is busy right now — please try again in a moment.";

    // LlmExceptions bubble up — a truncated/refused reply is never parsed.
    public async Task<PromptOutcome> PromptAsync(long userId, string text, LlmImage? image, CancellationToken ct = default)
    {
        // Full ⇒ busy NOW (the server's llmGate rule); the slot spans the whole turn, continuation
        // round included.
        if (!_gate.TryEnter())
        {
            throw new LlmException(BUSY_REPLY);
        }
        try
        {
            return await gatedPromptAsync(userId, text, image, ct);
        }
        finally
        {
            _gate.Exit();
        }
    }

    private async Task<PromptOutcome> gatedPromptAsync(long userId, string text, LlmImage? image, CancellationToken ct)
    {
        (PromptOutcome outcome, OpPlan? plan) = await roundAsync(userId, text, image, ct);
        // §7 auto-continuation: a plan that LOADED a picture planned blind, so re-send ONCE with the fresh
        // image. A plan that drew a layout committed to its coordinates and is not continued.
        if (plan is null || !continuablePlan(plan)) return outcome;
        LlmImage? fresh = await renderForVisionAsync(userId, ct);
        if (fresh is null) return outcome;
        // ChatDocument.CONTINUATION_NOTE, so this writer and the §12.1 gate that refuses it can
        // never drift apart.
        string note = text + "\n\n" + ChatDocument.CONTINUATION_NOTE;
        (PromptOutcome next, _) = await roundAsync(userId, note, fresh, ct);
        return next with
        {
            Warnings = [.. outcome.Warnings, .. next.Warnings],
            Renders = [.. outcome.Renders, .. next.Renders],
            Exports = [.. outcome.Exports, .. next.Exports],
            Mutated = outcome.Mutated || next.Mutated,
            ClearChatRequested = outcome.ClearChatRequested || next.ClearChatRequested,
        };
    }

    private async Task<(PromptOutcome Outcome, OpPlan? Plan)> roundAsync(
        long userId, string text, LlmImage? image, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(userId, ct);
        LlmImage? edgeMap = await buildEdgeMapAsync(userId, session, image, ct);
        IReadOnlyList<ServerProjectInfo>? listings = await listContextProjectsAsync(userId, session, ct);
        LlmChatRequest request = BuildTurn(userId, session, text, image, edgeMap, listings);
        LlmReply reply = await _llm.ChatAsync(request, ct);
        // Record the exchange first so even a bad plan keeps the conversation coherent.
        recordTurn(userId, text, image, reply.Text);
        OpPlanParseResult parsed = OpPlanParser.Parse(reply.Text);
        if (parsed.Plan is not OpPlan plan)
        {
            return (new PromptOutcome($"The AI answered with an invalid plan — {parsed.Error}. Nothing was changed.", parsed.Warnings, []), null);
        }
        return (await executeAsync(userId, plan, parsed.Warnings, ct), plan);
    }

    // §7: the plan loaded pixels and drew no layout, so the model has yet to see what it produced.
    private static bool continuablePlan(OpPlan plan) =>
        plan.Variants.Count == 0 && plan.Ask is null
        && plan.Actions.Any(a => a is BlankAction or FrameAction or OpenUrlAction)
        && !plan.Actions.Any(a => a is LayoutAction);

    private async Task<LlmImage?> renderForVisionAsync(long userId, CancellationToken ct)
    {
        if (_attachments is null) return null;
        try
        {
            RenderResult render = await _editing.RenderAsync(userId, ct);
            return await _attachments.LoadAsync(render.Path, ct);
        }
        catch (Exception)
        {
            return null;   // no snapshot → the first round's answer stands
        }
    }
}
