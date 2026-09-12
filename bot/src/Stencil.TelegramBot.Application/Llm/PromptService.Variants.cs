using System.Text;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Llm;

public sealed partial class PromptService
{
    // Independent CLI processes, started together and joined; the CLI adapter's spawn gate bounds
    // them.
    private async Task<List<PromptRender>> RenderVariantsAsync(long userId, OpPlan plan, UserSession after, CancellationToken ct)
    {
        List<(string Label, EditState Edits)> folded = new(plan.Variants.Count);
        foreach (OpVariant variant in plan.Variants)
        {
            EditState edits = after.Edits;
            // Variants branch from the post-actions state, so each re-maps through its own
            // crop/rotate steps only.
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

    // No session mutation; the variant's own crop/rotate steps are recorded for its layout lines.
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

    // enabled:false clears both axes (the session path's mapping); enabled:true changes nothing.
    private static EditState FoldFormula(EditState edits, FormulaAction formula)
    {
        if (formula.Enabled is bool enabled)
        {
            return enabled ? edits : edits with { FormulaX = null, FormulaY = null };
        }
        string? value = string.IsNullOrWhiteSpace(formula.Expr) ? null : formula.Expr;
        return formula.Axis == "y" ? edits with { FormulaY = value } : edits with { FormulaX = value };
    }

    private static EditState FoldCrop(EditState edits, CropAction crop, PlanFrameMapper mapper)
    {
        mapper.RecordCrop(crop.Spec);
        return edits with { CropSpec = crop.Spec, Album = false };
    }

    private static EditState FoldRotate(EditState edits, RotateAction rotate, PlanFrameMapper mapper)
    {
        mapper.RecordRotate(Turns(rotate));
        return edits with { Rotate = ((edits.Rotate + Turns(rotate)) % 4 + 4) % 4 };
    }

    // left is negative.
    private static int Turns(RotateAction rotate) =>
        rotate.Dir == "left" ? -rotate.Times : rotate.Times;

    private static string? FilterValue(FilterAction filter) => filter.Mode switch
    {
        "none" => null,
        "custom" => filter.Tint,
        _ => filter.Mode,
    };

    private static StencilLayout BuildLayout(LayoutAction layout, UserSession session) => new()
    {
        ImageWidth = session.OriginalWidth,
        ImageHeight = session.OriginalHeight,
        Lines = layout.Lines,
    };

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
