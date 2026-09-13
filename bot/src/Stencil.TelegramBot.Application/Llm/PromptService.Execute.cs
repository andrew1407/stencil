using System.Collections.Concurrent;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Llm;

public sealed partial class PromptService
{
    // A plan the user WROTE rather than a model's: `echoSource` is the text its openUrl hosts must
    // appear in, and it runs through the same pre-flight, executor and mapper as a model plan.
    internal Task<PromptOutcome> RunPlanAsync(
        long userId, OpPlan plan, IReadOnlyList<string> warnings, string echoSource, CancellationToken ct) =>
        executeAsync(userId, plan, warnings, ct, echoSource);

    // Pre-flight the whole plan (an invalid plan executes nothing); the main result is NOT rendered
    // here.
    private async Task<PromptOutcome> executeAsync(
        long userId, OpPlan plan, IReadOnlyList<string> warnings, CancellationToken ct, string? echoSource = null)
    {
        if (plan.Actions.Count == 0 && plan.Variants.Count == 0)
        {
            return new PromptOutcome(plan.Reply, warnings, [], plan.Ask);
        }
        UserSession session = await _store.GetAsync(userId, ct);
        // §13 tooth #2: a forbidden op never executes, even if a registry/parser slip ever let one
        // parse.
        if (ForbiddenOpError(plan) is string forbiddenError)
        {
            return new PromptOutcome($"{forbiddenError} Nothing was changed.", warnings, [], plan.Ask);
        }
        // §10 user-echo guard: the model may echo the user but can never introduce, complete or
        // rewrite a host.
        if (openUrlEchoError(userId, plan, echoSource) is string echoError)
        {
            return new PromptOutcome($"{echoError} Nothing was changed.", warnings, [], plan.Ask);
        }
        if (preflightError(session, plan) is string error)
        {
            return new PromptOutcome($"{error} Nothing was changed.", warnings, [], plan.Ask);
        }
        List<PromptRender> renders = new();
        List<PromptExport> exports = new();
        List<string> allWarnings = new(warnings);
        // §1: plan coordinates are in the snapshot frame the model was shown.
        ActionContext ctx = new(userId, renders, exports, createMapper(session), allWarnings);
        foreach (PlanAction action in plan.Actions)
        {
            await applyActionAsync(ctx, action, ct);
        }
        UserSession after = await _store.GetAsync(userId, ct);
        renders.AddRange(await renderVariantsAsync(userId, plan, after, ct));
        // A plan of settings/metadata/connection ops changed no pixels; one ending in clear left
        // nothing to render.
        bool touchedPixels = plan.Actions.Any(a => a is not (
            SaveAction or ConnectAction or DisconnectAction or ClearAction or LineStyleAction
            or RenameProjectAction or DescribeAction or BlankColorAction or ProjectColorAction
            or ExportAction or ClearChatAction));
        return new PromptOutcome(
            plan.Reply, allWarnings, renders, plan.Ask,
            Mutated: touchedPixels && after.HasImage,
            Exports: exports,
            // §10 clearChat is DEFERRED to the end of the turn, whatever its plan position.
            ClearChatRequested: plan.Actions.Any(static a => a is ClearChatAction));
    }

    // §13's executor-level gate over top-level AND variant actions. Null = the plan may run.
    public static string? ForbiddenOpError(OpPlan plan)
    {
        foreach (PlanAction action in plan.Actions.Concat(plan.Variants.SelectMany(static v => v.Actions)))
        {
            if (OpRegistry.ForbiddenOps.Contains(action.Op))
            {
                return $"The plan used \"{action.Op}\", which is never model-drivable on this surface.";
            }
        }
        return null;
    }

    // §10: every openUrl URL must appear VERBATIM in the USER's own messages (the current turn
    // included); assistant text and fetched content never count. Null = the plan may run.
    private string? openUrlEchoError(long userId, OpPlan plan, string? echoSource)
    {
        foreach (PlanAction action in plan.Actions)
        {
            if (action is OpenUrlAction open && !echoed(userId, open.Url, echoSource))
            {
                return $"The plan tried to open a URL you never wrote ({showServer(open.Url)}) — "
                    + "only a link from your own messages may be loaded.";
            }
        }
        return null;
    }

    // A script is the user's own text, so it stands in for the chat history it never went through.
    private bool echoed(long userId, string url, string? echoSource) =>
        echoSource is string source
            ? source.Contains(url, StringComparison.Ordinal)
            : urlEchoedByUser(userId, url);

    private bool urlEchoedByUser(long userId, string url)
    {
        foreach (LlmMessage message in snapshotHistory(userId))
        {
            if (message.Role == LlmMessage.ROLE_USER && message.Text.Contains(url, StringComparison.Ordinal))
            {
                return true;
            }
        }
        return false;
    }

    private static string? preflightError(UserSession session, OpPlan plan)
    {
        // A blank or an awaited URL load at the plan's head PRODUCES the working image.
        bool startsWithLoad = plan.Actions.Count > 0 && plan.Actions[0] is BlankAction or OpenUrlAction;
        // §10 ops (and §2 undo/redo) never need pixels up front — a plan made only of them runs
        // imageless.
        bool needsImage = plan.Variants.Count > 0
            || plan.Actions.Any(static a => a is not (
                ConnectAction or DisconnectAction or ClearAction or LineStyleAction
                or RenameProjectAction or DescribeAction or BlankColorAction or ProjectColorAction
                or ExportAction or UndoAction or RedoAction or ClearChatAction));
        if (!session.HasImage && needsImage && !startsWithLoad)
        {
            return "There is no working image — send a photo (or ask for a blank page) first.";
        }
        if (plan.Actions.Any(a => a is FrameAction) && session.VideoSourcePath is null)
        {
            // Contract §2: `frame` is only valid when the working input is a video.
            return "The \"frame\" op needs a video input — send a video first.";
        }
        foreach (OpVariant variant in plan.Variants)
        {
            PlanAction? unsupported = variant.Actions.FirstOrDefault(a => a is BlankAction or FrameAction);
            if (unsupported is not null)
            {
                return $"The bot can't apply \"{unsupported.Op}\" inside a variant — variants may only transform the working image.";
            }
        }
        return null;
    }

    // Seeded with the frame the model saw: stored crop on the original dims, dims swapped on an odd
    // rotation.
    private static PlanFrameMapper createMapper(UserSession session)
    {
        double w = session.OriginalWidth;
        double h = session.OriginalHeight;
        if (session.Edits.CropSpec is string spec
            && CropSpecResolver.Resolve(spec, w, h, session.Edits.Album) is CropRect rect)
        {
            (w, h) = (rect.Width, rect.Height);
        }
        if (session.Edits.Rotate % 2 != 0)
        {
            (w, h) = (h, w);
        }
        return new PlanFrameMapper(w, h);
    }

    private async Task resetMapperAsync(long userId, PlanFrameMapper mapper, CancellationToken ct)
    {
        UserSession fresh = await _store.GetAsync(userId, ct);
        mapper.Reset(fresh.OriginalWidth, fresh.OriginalHeight);
    }

    // Undo/redo/reset stepped the SESSION's crop/rotate — reseed at the frame it now resolves to.
    private async Task reseedMapperAsync(long userId, PlanFrameMapper mapper, CancellationToken ct)
    {
        UserSession fresh = await _store.GetAsync(userId, ct);
        PlanFrameMapper seeded = createMapper(fresh);
        mapper.Reset(seeded.Width, seeded.Height);
    }
}
