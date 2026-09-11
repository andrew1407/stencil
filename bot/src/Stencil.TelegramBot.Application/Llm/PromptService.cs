using System.Collections.Concurrent;
using System.Text;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Llm;

public sealed record PromptRender(string Label, RenderResult Result);

/// <summary>One §10 <c>export</c> document — the same bytes/name/caption <c>/json</c> and
/// <c>/project</c> send; one document per action, per the contract.</summary>
public sealed record PromptExport(string FileName, byte[] Bytes, string Caption);

/// <summary>
/// One prompt turn's outcome. The main result is NOT rendered here — on <see cref="Mutated"/>
/// the caller sends it through the shared render path; §10 <c>clearChat</c> is deferred.
/// </summary>
public sealed record PromptOutcome(
    string Reply,
    IReadOnlyList<string> Warnings,
    IReadOnlyList<PromptRender> Renders,
    AskCard? Ask = null,
    bool Mutated = false,
    IReadOnlyList<PromptExport>? Exports = null,
    bool ClearChatRequested = false)
{
    /// <summary>The §10 export documents (never null; empty on most turns).</summary>
    public IReadOnlyList<PromptExport> Exports { get; init; } = Exports ?? [];
}

/// <summary>
/// The <c>/prompt</c> engine: builds each chat turn per <c>llm-contract.md</c>, parses the reply
/// through <see cref="OpPlanParser"/>, and folds every action onto the SAME
/// <see cref="IEditingService"/> methods the slash commands use — LLM edits behave like manual ones.
/// </summary>
/// <remarks>
/// History is an in-memory per-user registry bounded to the contract's most recent 32 messages;
/// it holds base64 images, which is exactly why it stays out of the persisted session JSON.
/// Split into partial files: Turn (request assembly), History, Execute (plan execution), and
/// Actions (per-op appliers).
/// </remarks>
public sealed partial class PromptService
{
    /// <summary>The contract's history bound (§7): the most recent 32 messages are replayed.</summary>
    public const int MaxHistoryMessages = 32;

    /// <summary>Longest sanitized variant label kept for captions/file names.</summary>
    private const int MaxLabelChars = 40;

    /// <summary>How many users' conversations stay in memory; beyond it the least-recently-active
    /// one is forgotten (their next turn starts fresh).</summary>
    public const int MaxTrackedUsers = 256;

    /// <summary>One user's conversation plus a monotonic last-touched stamp for eviction.</summary>
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

    /// <summary>
    /// The provider config this user's turns run against: their <c>/chatapi</c> profile, else
    /// the operator's own — a profile since removed falls back rather than failing the turn.
    /// </summary>
    private LlmOptions OptionsFor(UserSession session) =>
        session.LlmProfile is not string name
            ? _options
            : _profiles.FirstOrDefault(p => string.Equals(p.Name, name, StringComparison.OrdinalIgnoreCase))?.Options
                ?? _options;

    public const string BusyReply = "The assistant is busy right now — please try again in a moment.";

    /// <summary>
    /// Run one prompt turn: build the request, call the LLM, parse the plan and execute it.
    /// <see cref="LlmException"/>s bubble up — a truncated/refused reply is never parsed.
    /// </summary>
    public async Task<PromptOutcome> PromptAsync(long userId, string text, LlmImage? image, CancellationToken ct = default)
    {
        // Full ⇒ busy NOW (the server's llmGate rule): queueing would hold the user for the
        // whole upstream timeout and answer late anyway. The slot spans the whole turn, so
        // the §7 continuation round can never go busy halfway through.
        if (!_gate.TryEnter())
        {
            throw new LlmException(BusyReply);
        }
        try
        {
            return await GatedPromptAsync(userId, text, image, ct);
        }
        finally
        {
            _gate.Exit();
        }
    }

    private async Task<PromptOutcome> GatedPromptAsync(long userId, string text, LlmImage? image, CancellationToken ct)
    {
        (PromptOutcome outcome, OpPlan? plan) = await RoundAsync(userId, text, image, ct);
        // Contract §7 auto-continuation: a plan that LOADED a picture the model has not seen
        // planned blind, so re-send ONCE with the fresh image, restating the request. A plan
        // that drew a layout committed to its coordinates and is not continued.
        if (plan is null || !ContinuablePlan(plan)) return outcome;
        LlmImage? fresh = await RenderForVisionAsync(userId, ct);
        if (fresh is null) return outcome;
        // The note is ChatDocument.ContinuationNote so this writer and the §12.1 gate that
        // refuses it in the persisted document can never drift apart.
        string note = text + "\n\n" + ChatDocument.ContinuationNote;
        (PromptOutcome next, _) = await RoundAsync(userId, note, fresh, ct);
        return next with
        {
            Warnings = [.. outcome.Warnings, .. next.Warnings],
            Renders = [.. outcome.Renders, .. next.Renders],
            Exports = [.. outcome.Exports, .. next.Exports],
            Mutated = outcome.Mutated || next.Mutated,
            ClearChatRequested = outcome.ClearChatRequested || next.ClearChatRequested,
        };
    }

    private async Task<(PromptOutcome Outcome, OpPlan? Plan)> RoundAsync(
        long userId, string text, LlmImage? image, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(userId, ct);
        LlmImage? edgeMap = await BuildEdgeMapAsync(userId, session, image, ct);
        IReadOnlyList<ServerProjectInfo>? listings = await ListContextProjectsAsync(userId, session, ct);
        LlmChatRequest request = BuildTurn(userId, session, text, image, edgeMap, listings);
        LlmReply reply = await _llm.ChatAsync(request, ct);
        // Record the exchange first so even a bad plan keeps the conversation coherent.
        RecordTurn(userId, text, image, reply.Text);
        OpPlanParseResult parsed = OpPlanParser.Parse(reply.Text);
        if (parsed.Plan is not OpPlan plan)
        {
            return (new PromptOutcome($"The AI answered with an invalid plan — {parsed.Error}. Nothing was changed.", parsed.Warnings, []), null);
        }
        return (await ExecuteAsync(userId, plan, parsed.Warnings, ct), plan);
    }

    /// <summary>§7: the plan loaded pixels (blank / frame / URL) and drew no layout, so the
    /// model has yet to see what it produced.</summary>
    private static bool ContinuablePlan(OpPlan plan) =>
        plan.Variants.Count == 0 && plan.Ask is null
        && plan.Actions.Any(a => a is BlankAction or FrameAction or OpenUrlAction)
        && !plan.Actions.Any(a => a is LayoutAction);

    private async Task<LlmImage?> RenderForVisionAsync(long userId, CancellationToken ct)
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
