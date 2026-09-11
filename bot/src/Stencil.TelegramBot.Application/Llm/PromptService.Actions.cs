using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Llm;

// PromptService — the registry dispatch plus the §2 image appliers (each folds onto the same
// service path its slash command uses). Class doc lives in PromptService.cs.
public sealed partial class PromptService
{
    /// <summary>
    /// Apply one top-level action through its <see cref="OpRegistry"/> entry — the same table
    /// that carries the op's prompt bullet, so nothing executes an op the prompt never listed.
    /// §2.1/§10 ops report what they could not do on <see cref="ActionContext.Warnings"/> —
    /// per-ACTION notes, never plan failures. An unregistered op is a no-op (§1's skip).
    /// </summary>
    private Task ApplyActionAsync(ActionContext ctx, PlanAction action, CancellationToken ct) =>
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
        ctx.Mapper.RecordRotate(Turns(rotate));
        await _editing.RotateAsync(ctx.UserId, Turns(rotate), ct);
    }

    internal Task ApplyFilterAsync(ActionContext ctx, FilterAction filter, CancellationToken ct) =>
        _editing.SetFilterAsync(ctx.UserId, FilterValue(filter), ct);

    internal async Task ApplyLayoutAsync(ActionContext ctx, LayoutAction layout, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(ctx.UserId, ct);
        LayoutAction remapped = new(ctx.Mapper.MapLines(layout.Lines));
        await _editing.ApplyLayoutAsync(ctx.UserId, BuildLayout(remapped, session), ct: ct);
    }

    /// <summary>§2: an ISO name, or custom cm dims — both through the <c>/format</c> path.</summary>
    internal Task ApplyPageAsync(ActionContext ctx, PageAction page, CancellationToken ct) =>
        page.Format is string format
            ? _editing.SetPageFormatAsync(ctx.UserId, format, null, null, ct)
            : _editing.SetPageFormatAsync(ctx.UserId, "custom", page.WidthCm, page.HeightCm, ct);

    /// <summary>
    /// §2 <c>blank</c>: explicit cm dims override the format — stored as the custom page size
    /// first, so BlankAsync converts them to pixels exactly like the CLI console.
    /// </summary>
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
        await ResetMapperAsync(ctx.UserId, ctx.Mapper, ct);
    }

    /// <summary>
    /// §2 <c>frame</c>: each index re-grabs the working image from the video; every frame but the
    /// last is rendered right away so multi-frame picks yield one image per frame (the last frame
    /// stays current and is covered by the main render).
    /// </summary>
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
        await ResetMapperAsync(ctx.UserId, ctx.Mapper, ct);
    }

    /// <summary>§2 <c>reset</c>: the <c>/reset</c> path — every pending edit dropped, the image kept.</summary>
    internal async Task ApplyResetAsync(ActionContext ctx, CancellationToken ct)
    {
        await _editing.ResetEditsAsync(ctx.UserId, ct);
        await ReseedMapperAsync(ctx.UserId, ctx.Mapper, ct);
    }

    /// <summary>
    /// §2 <c>formula</c>: axis+expr set/clear one axis; <c>enabled:false</c> switches formulas
    /// off entirely — for the bot that clears BOTH axes (its formulas have no kept-but-disabled
    /// state), restoring identity. <c>enabled:true</c> has nothing to re-enable and is a no-op.
    /// </summary>
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
