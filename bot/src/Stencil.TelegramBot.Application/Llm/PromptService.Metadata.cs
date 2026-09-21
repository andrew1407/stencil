using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Application.Llm.Plan;

namespace Stencil.TelegramBot.Application.Llm;

// §10 metadata ops write through to the server when a project is active, else hold the value for
// /create.
public sealed partial class PromptService
{
    // Misses and server errors (duplicates) are notes.
    internal async Task RenameProjectAsync(ActionContext ctx, RenameProjectAction rename, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(ctx.UserId, ct);
        if (_projects is not null && session.ActiveProjectId is not null)
        {
            try
            {
                await _projects.SetProjectNameAsync(ctx.UserId, rename.Name, ct);
            }
            catch (Exception ex) when (ex is not OperationCanceledException)
            {
                ctx.Warnings.Add($"Skipped renaming the project — {ex.Message}");
            }
            return;
        }
        if (!session.HasImage)
        {
            ctx.Warnings.Add("Skipped renaming — there is no working image to name.");
            return;
        }
        await _store.SaveAsync(session with { ImageLabel = rename.Name }, ct);
    }

    // "" clears. Misses are notes.
    internal async Task DescribeProjectAsync(ActionContext ctx, DescribeAction describe, CancellationToken ct)
    {
        string text = describe.Text.Trim();
        UserSession session = await _store.GetAsync(ctx.UserId, ct);
        if (_projects is not null && session.ActiveProjectId is not null)
        {
            try
            {
                await _projects.SetProjectDescriptionAsync(ctx.UserId, text, ct);
            }
            catch (Exception ex) when (ex is not OperationCanceledException)
            {
                ctx.Warnings.Add($"Skipped the description — {ex.Message}");
            }
            return;
        }
        if (!session.HasImage)
        {
            ctx.Warnings.Add("Skipped the description — there is no working image to describe.");
            return;
        }
        await _store.SaveAsync(session with { ActiveProjectDescription = text }, ct);
    }

    // KEEPS the edits (a fresh blank would destroy them); a non-blank or missing project is a note.
    internal async Task SetBlankColorAsync(ActionContext ctx, BlankColorAction blankColor, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(ctx.UserId, ct);
        if (_projects is null || session.ActiveProjectId is null)
        {
            ctx.Warnings.Add("Skipped the blank colour — no active server project (blanks recolour through /create + /blankcolor).");
            return;
        }
        try
        {
            string effective = await _projects.SetProjectBlankColorAsync(ctx.UserId, blankColor.Color, ct);
            if (effective.Length == 0)
            {
                ctx.Warnings.Add("Skipped the blank colour — this project is not a blank image.");
            }
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            ctx.Warnings.Add($"Skipped the blank colour — {ex.Message}");
        }
    }

    internal async Task SetProjectColorAsync(ActionContext ctx, ProjectColorAction projectColor, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(ctx.UserId, ct);
        if (_projects is null || session.ActiveProjectId is null)
        {
            ctx.Warnings.Add("Skipped the project colour — no active server project.");
            return;
        }
        try
        {
            await _projects.SetProjectColorAsync(ctx.UserId, projectColor.Color, ct);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            ctx.Warnings.Add($"Skipped the project colour — {ex.Message}");
        }
    }
}
