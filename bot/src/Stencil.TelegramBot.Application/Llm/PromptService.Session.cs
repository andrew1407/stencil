using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Llm;

public sealed partial class PromptService
{
    internal Task StepHistoryAsync(ActionContext ctx, PlanAction action, CancellationToken ct) =>
        action is RedoAction redo
            ? StepHistoryAsync(ctx, redo.Steps, redo: true, ct)
            : StepHistoryAsync(ctx, ((UndoAction)action).Steps, redo: false, ct);

    // Running out of history is a note, never a failed plan.
    private async Task StepHistoryAsync(ActionContext ctx, int steps, bool redo, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(ctx.UserId, ct);
        string verb = redo ? "redo" : "undo";
        if (!session.HasImage)
        {
            ctx.Warnings.Add($"Skipped {verb} — there is no working image.");
            return;
        }
        int available = redo ? session.EditRedo.Count : session.EditHistory.Count;
        int taken = Math.Min(steps, available);
        for (int i = 0; i < taken; i++)
        {
            await (redo ? _editing.RedoAsync(ctx.UserId, ct) : _editing.UndoAsync(ctx.UserId, ct));
        }
        if (available < steps)
        {
            ctx.Warnings.Add(available == 0
                ? $"Nothing to {verb} — the edit history is empty."
                : $"{char.ToUpperInvariant(verb[0])}{verb[1..]} stopped after {available} step(s) — no more history.");
        }
        // The visible frame may have changed (a crop/rotate stepped away) — reseed the mapper.
        await ReseedMapperAsync(ctx.UserId, ctx.Mapper, ct);
    }

    // A prompt turn carries exactly ONE image: index 1 drops the edits so far; a higher index is a
    // warning.
    internal async Task SwitchImageAsync(ActionContext ctx, ImageAction image, CancellationToken ct)
    {
        if (image.Index > 1)
        {
            ctx.Warnings.Add($"Skipped switching to attached image {image.Index} — this message attached 1 image.");
            return;
        }
        UserSession session = await _store.GetAsync(ctx.UserId, ct);
        if (!session.HasImage)
        {
            ctx.Warnings.Add("Skipped switching images — there is no attached image to switch to.");
            return;
        }
        await _editing.ResetEditsAsync(ctx.UserId, ct);
        await ResetMapperAsync(ctx.UserId, ctx.Mapper, ct);
    }

    // Through the ACTIVE server project (/save), renamed first when named; any failure is a
    // warning, never a failed plan.
    internal async Task SaveProjectAsync(ActionContext ctx, SaveAction save, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(ctx.UserId, ct);
        if (!session.HasImage)
        {
            ctx.Warnings.Add("Skipped save — there is no working image to save.");
            return;
        }
        if (_projects is null || session.ActiveProjectId is null)
        {
            ctx.Warnings.Add("Skipped save — no active server project. Connect a server and /create one to save from a prompt.");
            return;
        }
        // §10: a destination cannot be honoured here — the bot saves server projects.
        if (save.Path is not null)
        {
            ctx.Warnings.Add("Saved to the usual place — this surface cannot save to a path");
        }
        try
        {
            if (save.Name is string name && name.Trim().Length > 0
                && !string.Equals(name.Trim(), session.ActiveProjectName, StringComparison.Ordinal))
            {
                await _projects.SetProjectNameAsync(ctx.UserId, name.Trim(), ct);
            }
            await _projects.SaveActiveProjectAsync(ctx.UserId, ct);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            ctx.Warnings.Add($"Skipped save — the server rejected it: {ex.Message}");
        }
    }
}
