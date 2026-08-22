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

// PromptService — plan execution: ExecuteAsync with its §13/§10 gates and pre-flight,
// the snapshot-frame mapper, and variant folding. Class doc lives in PromptService.cs.
public sealed partial class PromptService
{
    /// <summary>
    /// Execute a validated plan: pre-flight it whole (an invalid plan executes nothing), fold the
    /// top-level actions through the editing service, then render each variant from a copy of the
    /// resulting <see cref="EditState"/>. The main result is NOT rendered here (see <see cref="PromptOutcome.Mutated"/>).
    /// </summary>
    private async Task<PromptOutcome> ExecuteAsync(long userId, OpPlan plan, IReadOnlyList<string> warnings, CancellationToken ct)
    {
        if (plan.Actions.Count == 0 && plan.Variants.Count == 0)
        {
            // Chat-only turn — no render.
            return new PromptOutcome(plan.Reply, warnings, [], plan.Ask);
        }
        UserSession session = await _store.GetAsync(userId, ct);
        // §13 tooth #2: a forbidden op never executes, even if a registry/parser slip ever
        // let one parse (the parser skips them as unknown ops today).
        if (ForbiddenOpError(plan) is string forbiddenError)
        {
            return new PromptOutcome($"{forbiddenError} Nothing was changed.", warnings, [], plan.Ask);
        }
        // §10 user-echo guard: an openUrl whose URL the user never wrote fails the WHOLE plan
        // — the model may echo the user but can never introduce, complete, or rewrite a host.
        if (OpenUrlEchoError(userId, plan) is string echoError)
        {
            return new PromptOutcome($"{echoError} Nothing was changed.", warnings, [], plan.Ask);
        }
        if (PreflightError(session, plan) is string error)
        {
            return new PromptOutcome($"{error} Nothing was changed.", warnings, [], plan.Ask);
        }
        List<PromptRender> renders = new();
        List<PromptExport> exports = new();
        List<string> allWarnings = new(warnings);
        // Contract §1: plan coordinates are in the snapshot frame the model was shown —
        // the mapper carries later layout points through the plan's own crop/rotate steps.
        PlanFrameMapper mapper = CreateMapper(session);
        foreach (PlanAction action in plan.Actions)
        {
            await ApplyActionAsync(userId, action, renders, exports, mapper, allWarnings, ct);
        }
        UserSession after = await _store.GetAsync(userId, ct);
        int number = 0;
        foreach (OpVariant variant in plan.Variants)
        {
            number++;
            EditState edits = after.Edits;
            // Variants branch from the post-actions state; their coordinates are in that
            // frame, so each variant re-maps through its own crop/rotate steps only.
            PlanFrameMapper variantMapper = CreateMapper(after);
            foreach (PlanAction action in variant.Actions)
            {
                edits = Fold(edits, action, after, variantMapper);
            }
            RenderResult result = await _editing.RenderAsync(userId, edits, ct);
            renders.Add(new PromptRender(SanitizeLabel(variant.Label, number), result));
        }
        // §2.1/§10: a plan that only saved, exported, managed connections or project metadata,
        // or adjusted the pen defaults changed no pixels — there is nothing new to send back.
        // A plan that ended with `clear` left no image to render either.
        bool touchedPixels = plan.Actions.Any(a => a is not (
            SaveAction or ConnectAction or DisconnectAction or ClearAction or LineStyleAction
            or RenameProjectAction or DescribeAction or BlankColorAction or ProjectColorAction
            or ExportAction or ClearChatAction));
        return new PromptOutcome(
            plan.Reply, allWarnings, renders, plan.Ask,
            Mutated: touchedPixels && after.HasImage,
            Exports: exports,
            // §10 clearChat is DEFERRED: nothing cleared here, whatever the op's plan position
            // — the caller confirms and clears at the end of the turn.
            ClearChatRequested: plan.Actions.Any(static a => a is ClearChatAction));
    }

    /// <summary>
    /// §13's executor-level forbidden-ops gate: the plan fails outright when any action —
    /// top-level or inside a variant — carries an op from <see cref="OpRegistry.ForbiddenOps"/>.
    /// Null = the plan may run.
    /// </summary>
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

    /// <summary>
    /// The §10 user-echo guard, plan-level: every <c>openUrl</c> URL must appear VERBATIM in
    /// the USER's own messages of this conversation (the current turn included — it is already
    /// recorded). Assistant text and fetched/attached content never count. Null = the plan may run.
    /// </summary>
    private string? OpenUrlEchoError(long userId, OpPlan plan)
    {
        foreach (PlanAction action in plan.Actions)
        {
            if (action is OpenUrlAction open && !UrlEchoedByUser(userId, open.Url))
            {
                return $"The plan tried to open a URL you never wrote ({Shown(open.Url)}) — "
                    + "only a link from your own messages may be loaded.";
            }
        }
        return null;
    }

    /// <summary>True when <paramref name="url"/> appears verbatim in a USER turn of the history.</summary>
    private bool UrlEchoedByUser(long userId, string url)
    {
        foreach (LlmMessage message in SnapshotHistory(userId))
        {
            if (message.Role == LlmMessage.RoleUser && message.Text.Contains(url, StringComparison.Ordinal))
            {
                return true;
            }
        }
        return false;
    }

    /// <summary>A plan-level executability error, or null when the plan can run.</summary>
    private static string? PreflightError(UserSession session, OpPlan plan)
    {
        // A blank or an awaited URL load at the plan's head PRODUCES the working image.
        bool startsWithLoad = plan.Actions.Count > 0 && plan.Actions[0] is BlankAction or OpenUrlAction;
        // §10: connection/settings/metadata ops (and §2 undo/redo, whose empty-history miss is
        // a warning) never need pixels up front — a plan made only of them runs imageless.
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

    /// <summary>
    /// The mapper seeded with the frame the model was shown: the stored crop resolved against
    /// the original dims, then the dims swapped on an odd rotation (the CLI's crop-then-rotate
    /// order). No steps recorded; plan actions add those as they execute.
    /// </summary>
    private static PlanFrameMapper CreateMapper(UserSession session)
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

    /// <summary>A blank/frame/URL load replaced the working image — restart the mapper at its dims.</summary>
    private async Task ResetMapperAsync(long userId, PlanFrameMapper mapper, CancellationToken ct)
    {
        UserSession fresh = await _store.GetAsync(userId, ct);
        mapper.Reset(fresh.OriginalWidth, fresh.OriginalHeight);
    }

    /// <summary>
    /// Undo/redo/reset stepped the SESSION's own crop/rotate state — restart the mapper at the
    /// frame the session now resolves to (the same seed <see cref="CreateMapper"/> computes).
    /// </summary>
    private async Task ReseedMapperAsync(long userId, PlanFrameMapper mapper, CancellationToken ct)
    {
        UserSession fresh = await _store.GetAsync(userId, ct);
        PlanFrameMapper seeded = CreateMapper(fresh);
        mapper.Reset(seeded.Width, seeded.Height);
    }

    /// <summary>
    /// Fold a variant action into an <see cref="EditState"/> copy (no session mutation),
    /// recording the variant's own crop/rotate frame steps so its layout lines re-map from
    /// the post-actions snapshot frame like top-level ones do.
    /// </summary>
    private static EditState Fold(EditState edits, PlanAction action, UserSession session, PlanFrameMapper mapper) => action switch
    {
        CropAction crop => FoldCrop(edits, crop, mapper),
        RotateAction rotate => FoldRotate(edits, rotate, mapper),
        FilterAction filter => edits with { Filter = FilterValue(filter) },
        LayoutAction layout => edits with { Layout = BuildLayout(new LayoutAction(mapper.MapLines(layout.Lines)), session) },
        FormulaAction formula => FoldFormula(edits, formula),
        PageAction page => page.Format is string format
            ? edits with { PageFormat = format, CustomPageWidth = null, CustomPageHeight = null }
            : edits with { PageFormat = "custom", CustomPageWidth = page.WidthCm, CustomPageHeight = page.HeightCm },
        // blank/frame are rejected by the pre-flight; anything else would be a parser bug.
        _ => throw new InvalidOperationException($"The bot can't apply \"{action.Op}\" inside a variant."),
    };

    /// <summary>
    /// Fold a variant formula: axis+expr sets/clears one axis; <c>enabled:false</c> clears
    /// both (the session path's mapping); <c>enabled:true</c> changes nothing.
    /// </summary>
    private static EditState FoldFormula(EditState edits, FormulaAction formula)
    {
        if (formula.Enabled is bool enabled)
        {
            return enabled ? edits : edits with { FormulaX = null, FormulaY = null };
        }
        string? value = string.IsNullOrWhiteSpace(formula.Expr) ? null : formula.Expr;
        return formula.Axis == "y" ? edits with { FormulaY = value } : edits with { FormulaX = value };
    }

    /// <summary>Fold a variant crop: record the frame step, then store the spec.</summary>
    private static EditState FoldCrop(EditState edits, CropAction crop, PlanFrameMapper mapper)
    {
        mapper.RecordCrop(crop.Spec);
        return edits with { CropSpec = crop.Spec, Album = false };
    }

    /// <summary>Fold a variant rotation: record the frame step, then accumulate the turns.</summary>
    private static EditState FoldRotate(EditState edits, RotateAction rotate, PlanFrameMapper mapper)
    {
        mapper.RecordRotate(Turns(rotate));
        return edits with { Rotate = ((edits.Rotate + Turns(rotate)) % 4 + 4) % 4 };
    }

    /// <summary>Clockwise quarter turns for the editing service: <c>left</c> is negative.</summary>
    private static int Turns(RotateAction rotate) =>
        rotate.Dir == "left" ? -rotate.Times : rotate.Times;

    /// <summary>The editing service's filter value: none clears, custom passes the tint colour.</summary>
    private static string? FilterValue(FilterAction filter) => filter.Mode switch
    {
        "none" => null,
        "custom" => filter.Tint,
        _ => filter.Mode,
    };

    /// <summary>A full layout carrying the working image's dimensions (like /draw does).</summary>
    private static StencilLayout BuildLayout(LayoutAction layout, UserSession session) => new()
    {
        ImageWidth = session.OriginalWidth,
        ImageHeight = session.OriginalHeight,
        Lines = layout.Lines,
    };

    /// <summary>Sanitize a variant label for captions/file names; empty falls back to "variant N".</summary>
    private static string SanitizeLabel(string label, int number)
    {
        StringBuilder sb = new();
        foreach (char c in label)
        {
            if (char.IsLetterOrDigit(c) || c is '-' or '_' or ' ')
            {
                sb.Append(c);
            }
            if (sb.Length >= MaxLabelChars)
            {
                break;
            }
        }
        string cleaned = sb.ToString().Trim();
        return cleaned.Length == 0 ? $"variant {number}" : cleaned;
    }
}
