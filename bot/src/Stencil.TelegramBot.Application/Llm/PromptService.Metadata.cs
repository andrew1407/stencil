using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Llm;

// PromptService — the §10 project-metadata ops (name, description, colours). Each writes
// through to the server when a project is active, else holds the value on the session for the
// eventual /create. Class doc lives in PromptService.cs.
public sealed partial class PromptService
{
    /// <summary>
    /// §10 <c>renameProject</c>: the <c>/projectname</c> path — a saved server project renames
    /// on the server (version-guarded); an unsaved working image is relabelled locally (the
    /// name <c>/create</c> will use). Misses and server errors (duplicates) are notes.
    /// </summary>
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

    /// <summary>
    /// §10 <c>describe</c>: the <c>/projectdescription</c> path — a saved server project
    /// writes through to the server; an unsaved image holds the text locally for <c>/create</c>.
    /// <c>""</c> clears. Misses are notes.
    /// </summary>
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

    /// <summary>
    /// §10 <c>blankColor</c>: the <c>/blankcolor</c> path — recolour the active BLANK server
    /// project's background, KEEPING the edits (a fresh <c>blank</c> would destroy them). A
    /// non-blank project, a missing project, or a server error is a note.
    /// </summary>
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

    /// <summary>
    /// §10 <c>projectColor</c>: the <c>/projectcolor</c> path — the active server project's
    /// name colour; <c>""</c> clears it. Misses are notes.
    /// </summary>
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
