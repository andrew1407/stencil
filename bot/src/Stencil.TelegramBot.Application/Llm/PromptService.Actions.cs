using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Llm;

public sealed partial class PromptService
{
    // Dispatch through the OpRegistry entry carrying the op's bullet, so nothing executes an op the prompt
    // never listed. §2.1/§10 misses are per-ACTION notes, never plan failures.
    private Task applyActionAsync(ActionContext ctx, PlanAction action, CancellationToken ct) =>
        OpRegistry.HandlerFor(action.Op) is OpHandler handler
            ? handler(this, action, ctx, ct)
            : Task.CompletedTask;

    internal async Task ApplyCropAsync(ActionContext ctx, CropAction crop, CancellationToken ct)
    {
        ctx.Mapper.RecordCrop(crop.Spec);
        await _editing.SetCropAsync(ctx.UserId, crop.Spec, album: false, ct);
    }

    internal async Task ApplyRotateAsync(ActionContext ctx, RotateAction rotate, CancellationToken ct)
    {
        ctx.Mapper.RecordRotate(turns(rotate));
        await _editing.RotateAsync(ctx.UserId, turns(rotate), ct);
    }

    internal Task ApplyFilterAsync(ActionContext ctx, FilterAction filter, CancellationToken ct) =>
        _editing.SetFilterAsync(ctx.UserId, filterValue(filter), ct);

    internal async Task ApplyLayoutAsync(ActionContext ctx, LayoutAction layout, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(ctx.UserId, ct);
        LayoutAction remapped = new(ctx.Mapper.MapLines(layout.Lines));
        await _editing.ApplyLayoutAsync(ctx.UserId, buildLayout(remapped, session), ct: ct);
    }

    internal Task ApplyPageAsync(ActionContext ctx, PageAction page, CancellationToken ct) =>
        page.Format is string format
            ? _editing.SetPageFormatAsync(ctx.UserId, format, null, null, ct)
            : _editing.SetPageFormatAsync(ctx.UserId, "custom", page.WidthCm, page.HeightCm, ct);

    // Explicit cm dims are stored as the custom page size first, so BlankAsync converts them like
    // the CLI console.
    internal async Task ApplyBlankAsync(ActionContext ctx, BlankAction blank, CancellationToken ct)
    {
        if (blank.WidthCm is double widthCm && blank.HeightCm is double heightCm)
        {
            await _editing.SetPageFormatAsync(ctx.UserId, "custom", widthCm, heightCm, ct);
            await _editing.BlankAsync(ctx.UserId, new BlankSpec(null, null, blank.Color, null), ct);
        }
        else
        {
            await _editing.BlankAsync(ctx.UserId, new BlankSpec(null, null, blank.Color, blank.Format), ct);
        }
        await resetMapperAsync(ctx.UserId, ctx.Mapper, ct);
    }

    // Every frame but the last is rendered right away; the last stays current for the main render.
    internal async Task ApplyFrameAsync(ActionContext ctx, FrameAction frame, CancellationToken ct)
    {
        for (int i = 0; i < frame.Indices.Count; i++)
        {
            await _editing.ExtractFrameAsync(ctx.UserId, frame.Indices[i], ct);
            if (i < frame.Indices.Count - 1)
            {
                RenderResult result = await _editing.RenderAsync(ctx.UserId, ct);
                ctx.Renders.Add(new PromptRender($"frame {frame.Indices[i]}", result));
            }
        }
        await resetMapperAsync(ctx.UserId, ctx.Mapper, ct);
    }

    internal async Task ApplyResetAsync(ActionContext ctx, CancellationToken ct)
    {
        await _editing.ResetEditsAsync(ctx.UserId, ct);
        await reseedMapperAsync(ctx.UserId, ctx.Mapper, ct);
    }

    // enabled:false clears BOTH axes (the bot has no kept-but-disabled state); enabled:true is a
    // no-op.
    internal async Task ApplyFormulaAsync(ActionContext ctx, FormulaAction formula, CancellationToken ct)
    {
        if (formula.Enabled is bool enabled)
        {
            if (!enabled)
            {
                await _editing.SetFormulaAsync(ctx.UserId, "x", "", ct);
                await _editing.SetFormulaAsync(ctx.UserId, "y", "", ct);
            }
            return;
        }
        await _editing.SetFormulaAsync(ctx.UserId, formula.Axis!, formula.Expr!, ct);
    }
}
