using System.Text;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Llm;

// PromptService — variant rendering and the fold helpers it shares with the top-level
// appliers. Class doc lives in PromptService.cs.
public sealed partial class PromptService
{
    /// <summary>
    /// Render every variant from its own copy of the post-actions <see cref="EditState"/>. The
    /// runs are independent CLI processes, so they are started together and joined rather than
    /// paid for end to end; the CLI adapter's spawn gate bounds how many run at once.
    /// </summary>
    private async Task<List<PromptRender>> RenderVariantsAsync(long userId, OpPlan plan, UserSession after, CancellationToken ct)
    {
        List<(string Label, EditState Edits)> folded = new(plan.Variants.Count);
        foreach (OpVariant variant in plan.Variants)
        {
            EditState edits = after.Edits;
            // Variants branch from the post-actions state; their coordinates are in that
            // frame, so each variant re-maps through its own crop/rotate steps only.
            PlanFrameMapper variantMapper = CreateMapper(after);
            foreach (PlanAction action in variant.Actions)
            {
                edits = Fold(edits, action, after, variantMapper);
            }
            folded.Add((SanitizeLabel(variant.Label, folded.Count + 1), edits));
        }
        // Fold everything before spawning anything: a rejected action can't orphan a started run.
        List<Task<RenderResult>> renders = [.. folded.Select(f => _editing.RenderAsync(userId, f.Edits, ct))];
        // WhenAll observes every run, and reports the first failure in plan order.
        await Task.WhenAll(renders);
        return [.. folded.Select((f, i) => new PromptRender(f.Label, renders[i].Result))];
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
    }}
