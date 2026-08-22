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

// PromptService — per-op appliers: ApplyActionAsync and the §2.1/§10 action helpers
// (each folds onto the same service path its slash command uses). Class doc lives in
// PromptService.cs.
public sealed partial class PromptService
{
    /// <summary>
    /// Apply one top-level action through the same editing-service path its slash command uses,
    /// recording frame steps on <paramref name="mapper"/>. §2.1 and §10 ops report what they
    /// could not do on <paramref name="warnings"/> — per-ACTION notes, never plan failures.
    /// </summary>
    private async Task ApplyActionAsync(
        long userId, PlanAction action, List<PromptRender> renders, List<PromptExport> exports,
        PlanFrameMapper mapper, List<string> warnings, CancellationToken ct)
    {
        switch (action)
        {
            case CropAction crop:
                mapper.RecordCrop(crop.Spec);
                await _editing.SetCropAsync(userId, crop.Spec, album: false, ct);
                break;
            case RotateAction rotate:
                mapper.RecordRotate(Turns(rotate));
                await _editing.RotateAsync(userId, Turns(rotate), ct);
                break;
            case FilterAction filter:
                await _editing.SetFilterAsync(userId, FilterValue(filter), ct);
                break;
            case LayoutAction layout:
                UserSession session = await _store.GetAsync(userId, ct);
                LayoutAction remapped = new(mapper.MapLines(layout.Lines));
                await _editing.ApplyLayoutAsync(userId, BuildLayout(remapped, session), ct: ct);
                break;
            case FormulaAction formula:
                await ApplyFormulaAsync(userId, formula, ct);
                break;
            case PageAction page:
                // §2: an ISO name, or custom cm dims — both through the /format path.
                await (page.Format is string format
                    ? _editing.SetPageFormatAsync(userId, format, null, null, ct)
                    : _editing.SetPageFormatAsync(userId, "custom", page.WidthCm, page.HeightCm, ct));
                break;
            case BlankAction blank:
                // §2: explicit cm dims override the format — stored as the custom page size
                // first, so BlankAsync converts them to pixels exactly like the CLI console.
                if (blank.WidthCm is double widthCm && blank.HeightCm is double heightCm)
                {
                    await _editing.SetPageFormatAsync(userId, "custom", widthCm, heightCm, ct);
                    await _editing.BlankAsync(userId, new BlankSpec(null, null, blank.Color, null), ct);
                }
                else
                {
                    await _editing.BlankAsync(userId, new BlankSpec(null, null, blank.Color, blank.Format), ct);
                }
                await ResetMapperAsync(userId, mapper, ct);
                break;
            case FrameAction frame:
                // Each index re-grabs the working image from the video; every frame but the
                // last is rendered right away so multi-frame picks yield one image per frame
                // (the last frame stays current and is covered by the main render).
                for (int i = 0; i < frame.Indices.Count; i++)
                {
                    await _editing.ExtractFrameAsync(userId, frame.Indices[i], ct);
                    if (i < frame.Indices.Count - 1)
                    {
                        RenderResult result = await _editing.RenderAsync(userId, ct);
                        renders.Add(new PromptRender($"frame {frame.Indices[i]}", result));
                    }
                }
                await ResetMapperAsync(userId, mapper, ct);
                break;
            case ImageAction image:
                await SwitchImageAsync(userId, image, mapper, warnings, ct);
                break;
            case SaveAction save:
                await SaveProjectAsync(userId, save, warnings, ct);
                break;
            case ConnectAction connect:
                await ConnectServerAsync(userId, connect, warnings, ct);
                break;
            case DisconnectAction disconnect:
                await DisconnectServerAsync(userId, disconnect, warnings, ct);
                break;
            case UndoAction undo:
                await StepHistoryAsync(userId, undo.Steps, redo: false, mapper, warnings, ct);
                break;
            case RedoAction redo:
                await StepHistoryAsync(userId, redo.Steps, redo: true, mapper, warnings, ct);
                break;
            case ResetAction:
                // The /reset path: every pending edit dropped, the working image kept.
                await _editing.ResetEditsAsync(userId, ct);
                await ReseedMapperAsync(userId, mapper, ct);
                break;
            case ClearAction:
                await ClearImageAsync(userId, warnings, ct);
                break;
            case LineStyleAction lineStyle:
                await ConfigurePenAsync(userId, lineStyle, warnings, ct);
                break;
            case OpenUrlAction open:
                await OpenUrlAsync(userId, open, mapper, warnings, ct);
                break;
            case RenameProjectAction rename:
                await RenameProjectAsync(userId, rename, warnings, ct);
                break;
            case DescribeAction describe:
                await DescribeProjectAsync(userId, describe, warnings, ct);
                break;
            case BlankColorAction blankColor:
                await SetBlankColorAsync(userId, blankColor, warnings, ct);
                break;
            case ProjectColorAction projectColor:
                await SetProjectColorAsync(userId, projectColor, warnings, ct);
                break;
            case ExportAction export:
                await ExportAsync(userId, export, exports, warnings, ct);
                break;
            case ClearChatAction:
                // §10 clearChat executes nothing here — ExecuteAsync surfaces the deferred
                // request on the outcome once the plan's other actions are done.
                break;
        }
    }

    /// <summary>
    /// §2 <c>undo</c>/<c>redo</c>: step the pending-edit stacks through the same service calls
    /// the <c>/undo</c>/<c>/redo</c> commands use. Running out of history is a note, never a
    /// failed plan.
    /// </summary>
    private async Task StepHistoryAsync(
        long userId, int steps, bool redo, PlanFrameMapper mapper, List<string> warnings, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(userId, ct);
        string verb = redo ? "redo" : "undo";
        if (!session.HasImage)
        {
            warnings.Add($"Skipped {verb} — there is no working image.");
            return;
        }
        int available = redo ? session.EditRedo.Count : session.EditHistory.Count;
        int taken = Math.Min(steps, available);
        for (int i = 0; i < taken; i++)
        {
            await (redo ? _editing.RedoAsync(userId, ct) : _editing.UndoAsync(userId, ct));
        }
        if (available < steps)
        {
            warnings.Add(available == 0
                ? $"Nothing to {verb} — the edit history is empty."
                : $"{char.ToUpperInvariant(verb[0])}{verb[1..]} stopped after {available} step(s) — no more history.");
        }
        // The visible frame may have changed (a crop/rotate stepped away) — reseed the mapper.
        await ReseedMapperAsync(userId, mapper, ct);
    }

    /// <summary>
    /// §10 <c>clear</c>, scoped to the IMAGE AND EDITS ONLY: the same image wipe <c>/drop</c>
    /// performs — but NEVER the <c>/drop</c> chat wipe. The assistant conversation survives
    /// (clearing IT is the separate, user-confirmed <c>clearChat</c> op).
    /// </summary>
    private async Task ClearImageAsync(long userId, List<string> warnings, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(userId, ct);
        if (!session.HasImage)
        {
            warnings.Add("Skipped clear — there is no working image to remove.");
            return;
        }
        await _editing.DropImageAsync(userId, ct);
    }

    /// <summary>
    /// §10 <c>lineStyle</c>: the pen-default paths of <c>/color</c> <c>/thickness</c>
    /// <c>/points</c> <c>/style</c> <c>/fill</c>, in one call. The §10 fields the bot's pen
    /// doesn't model (pointColor, drawMode) are noted and skipped.
    /// </summary>
    private async Task ConfigurePenAsync(long userId, LineStyleAction pen, List<string> warnings, CancellationToken ct)
    {
        if (pen.PointColor is not null)
        {
            warnings.Add("Skipped \"pointColor\" — the bot's pen has no separate point colour.");
        }
        if (pen.DrawMode is not null)
        {
            warnings.Add("Skipped \"drawMode\" — the bot's pen has no draw-mode default.");
        }
        if (pen.Color is null && pen.Thickness is null && pen.PointSize is null
            && pen.Style is null && pen.FillColor is null)
        {
            return;   // only unsupported fields — nothing to configure
        }
        await _editing.ConfigurePenAsync(
            userId, pen.Color, pen.Thickness, pen.PointSize, pen.Style, pen.FillColor, ct);
    }

    /// <summary>
    /// §10 <c>openUrl</c>: the <c>/url</c> load path with its SSRF vetting intact (the echo guard
    /// already passed). The load is AWAITED so later actions never race it; a vetting or fetch
    /// failure is a warning and the plan continues on the previous image.
    /// </summary>
    private async Task OpenUrlAsync(
        long userId, OpenUrlAction open, PlanFrameMapper mapper, List<string> warnings, CancellationToken ct)
    {
        try
        {
            await _editing.SetImageFromUrlAsync(userId, open.Url, LabelFromUrl(open.Url), ct);
            await ResetMapperAsync(userId, mapper, ct);
            if (open.Incognito)
            {
                warnings.Add("Ignored \"incognito\" — a Telegram chat has no incognito mode.");
            }
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            warnings.Add($"Skipped opening {Shown(open.Url)} — {ex.Message}");
        }
    }

    /// <summary>
    /// §10 <c>renameProject</c>: the <c>/projectname</c> path — a saved server project renames
    /// on the server (version-guarded); an unsaved working image is relabelled locally (the
    /// name <c>/create</c> will use). Misses and server errors (duplicates) are notes.
    /// </summary>
    private async Task RenameProjectAsync(long userId, RenameProjectAction rename, List<string> warnings, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(userId, ct);
        if (_projects is not null && session.ActiveProjectId is not null)
        {
            try
            {
                await _projects.SetProjectNameAsync(userId, rename.Name, ct);
            }
            catch (Exception ex) when (ex is not OperationCanceledException)
            {
                warnings.Add($"Skipped renaming the project — {ex.Message}");
            }
            return;
        }
        if (!session.HasImage)
        {
            warnings.Add("Skipped renaming — there is no working image to name.");
            return;
        }
        await _store.SaveAsync(session with { ImageLabel = rename.Name }, ct);
    }

    /// <summary>
    /// §10 <c>describe</c>: the <c>/projectdescription</c> path — a saved server project
    /// writes through to the server; an unsaved image holds the text locally for <c>/create</c>.
    /// <c>""</c> clears. Misses are notes.
    /// </summary>
    private async Task DescribeProjectAsync(long userId, DescribeAction describe, List<string> warnings, CancellationToken ct)
    {
        string text = describe.Text.Trim();
        UserSession session = await _store.GetAsync(userId, ct);
        if (_projects is not null && session.ActiveProjectId is not null)
        {
            try
            {
                await _projects.SetProjectDescriptionAsync(userId, text, ct);
            }
            catch (Exception ex) when (ex is not OperationCanceledException)
            {
                warnings.Add($"Skipped the description — {ex.Message}");
            }
            return;
        }
        if (!session.HasImage)
        {
            warnings.Add("Skipped the description — there is no working image to describe.");
            return;
        }
        await _store.SaveAsync(session with { ActiveProjectDescription = text }, ct);
    }

    /// <summary>
    /// §10 <c>blankColor</c>: the <c>/blankcolor</c> path — recolour the active BLANK server
    /// project's background, KEEPING the edits (a fresh <c>blank</c> would destroy them). A
    /// non-blank project, a missing project, or a server error is a note.
    /// </summary>
    private async Task SetBlankColorAsync(long userId, BlankColorAction blankColor, List<string> warnings, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(userId, ct);
        if (_projects is null || session.ActiveProjectId is null)
        {
            warnings.Add("Skipped the blank colour — no active server project (blanks recolour through /create + /blankcolor).");
            return;
        }
        try
        {
            string effective = await _projects.SetProjectBlankColorAsync(userId, blankColor.Color, ct);
            if (effective.Length == 0)
            {
                warnings.Add("Skipped the blank colour — this project is not a blank image.");
            }
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            warnings.Add($"Skipped the blank colour — {ex.Message}");
        }
    }

    /// <summary>
    /// §10 <c>projectColor</c>: the <c>/projectcolor</c> path — the active server project's
    /// name colour; <c>""</c> clears it. Misses are notes.
    /// </summary>
    private async Task SetProjectColorAsync(long userId, ProjectColorAction projectColor, List<string> warnings, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(userId, ct);
        if (_projects is null || session.ActiveProjectId is null)
        {
            warnings.Add("Skipped the project colour — no active server project.");
            return;
        }
        try
        {
            await _projects.SetProjectColorAsync(userId, projectColor.Color, ct);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            warnings.Add($"Skipped the project colour — {ex.Message}");
        }
    }

    /// <summary>
    /// §10 <c>export</c>: build the SAME document <c>/json</c> / <c>/project</c> send — the
    /// layout JSON or the portable <c>.stencil</c> bundle — for the caller to send into the
    /// user's own chat, bounded to one send per action. Misses are notes.
    /// </summary>
    private async Task ExportAsync(
        long userId, ExportAction export, List<PromptExport> exports, List<string> warnings, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(userId, ct);
        if (!session.HasImage)
        {
            warnings.Add("Skipped export — there is no working image to export.");
            return;
        }
        try
        {
            if (export.What == "layout")
            {
                string json = _editing.ExportLayoutJson(session);
                exports.Add(new PromptExport(
                    SafeLabel(session.ImageLabel) + ".json", Encoding.UTF8.GetBytes(json), "Layout JSON"));
            }
            else
            {
                byte[] bytes = await _editing.ExportProjectFileAsync(userId, ct);
                exports.Add(new PromptExport(
                    SafeLabel(session.ImageLabel) + ".stencil", bytes, "Stencil project"));
            }
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            warnings.Add($"Skipped export — {ex.Message}");
        }
    }

    /// <summary>A short human label for a URL source: its file name, else its host (the /url rule).</summary>
    private static string LabelFromUrl(string url)
    {
        if (Uri.TryCreate(url, UriKind.Absolute, out Uri? uri))
        {
            string name = Path.GetFileName(uri.LocalPath);
            return name.Length > 0 ? name : uri.Host;
        }
        return "image";
    }

    /// <summary>A filesystem-safe stem for an export file name (the /json rule; defaults to "layout").</summary>
    private static string SafeLabel(string? label)
    {
        if (string.IsNullOrWhiteSpace(label))
        {
            return "layout";
        }
        StringBuilder sb = new();
        foreach (char c in label)
        {
            sb.Append(char.IsLetterOrDigit(c) || c is '-' or '_' ? c : '_');
        }
        string cleaned = sb.ToString().Trim('_');
        return cleaned.Length == 0 ? "layout" : cleaned;
    }

    /// <summary>
    /// §2 <c>formula</c>: axis+expr set/clear one axis; <c>enabled:false</c> switches formulas
    /// off entirely — for the bot that clears BOTH axes (its formulas have no kept-but-disabled
    /// state), restoring identity. <c>enabled:true</c> has nothing to re-enable and is a no-op.
    /// </summary>
    private async Task ApplyFormulaAsync(long userId, FormulaAction formula, CancellationToken ct)
    {
        if (formula.Enabled is bool enabled)
        {
            if (!enabled)
            {
                await _editing.SetFormulaAsync(userId, "x", "", ct);
                await _editing.SetFormulaAsync(userId, "y", "", ct);
            }
            return;
        }
        await _editing.SetFormulaAsync(userId, formula.Axis!, formula.Expr!, ct);
    }

    /// <summary>
    /// §2.1 <c>image</c>: switch to the turn's Nth attached image. A prompt turn here carries
    /// exactly ONE image, so index 1 drops the edits made so far; a higher index (albums batch
    /// one prompt run PER photo in the adapter) is skipped with a warning, never a failed plan.
    /// </summary>
    private async Task SwitchImageAsync(
        long userId, ImageAction image, PlanFrameMapper mapper, List<string> warnings, CancellationToken ct)
    {
        if (image.Index > 1)
        {
            warnings.Add($"Skipped switching to attached image {image.Index} — this message attached 1 image.");
            return;
        }
        UserSession session = await _store.GetAsync(userId, ct);
        if (!session.HasImage)
        {
            warnings.Add("Skipped switching images — there is no attached image to switch to.");
            return;
        }
        await _editing.ResetEditsAsync(userId, ct);
        await ResetMapperAsync(userId, mapper, ct);
    }

    /// <summary>
    /// §2.1 <c>save</c>: persist through the ACTIVE server project (the <c>/save</c> flow),
    /// renamed first when the plan named one. No active project/image — and any server failure —
    /// is a warning, never a failed plan, so it can never swallow the turn's reply.
    /// </summary>
    private async Task SaveProjectAsync(long userId, SaveAction save, List<string> warnings, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(userId, ct);
        if (!session.HasImage)
        {
            warnings.Add("Skipped save — there is no working image to save.");
            return;
        }
        if (_projects is null || session.ActiveProjectId is null)
        {
            warnings.Add("Skipped save — no active server project. Connect a server and /create one to save from a prompt.");
            return;
        }
        // §10: a destination cannot be honoured here — the bot saves server projects.
        if (save.Path is not null)
        {
            warnings.Add("Saved to the usual place — this surface cannot save to a path");
        }
        try
        {
            if (save.Name is string name && name.Trim().Length > 0
                && !string.Equals(name.Trim(), session.ActiveProjectName, StringComparison.Ordinal))
            {
                await _projects.SetProjectNameAsync(userId, name.Trim(), ct);
            }
            await _projects.SaveActiveProjectAsync(userId, ct);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            warnings.Add($"Skipped save — the server rejected it: {ex.Message}");
        }
    }

    /// <summary>
    /// §10 <c>connect</c>: re-run <c>/connect</c> for a server the user ALREADY saved, with the
    /// STORED token riding along — the model can never introduce a new host and plans never
    /// carry tokens. An unknown/ambiguous/refusing server is a warning, never a failed plan.
    /// </summary>
    private async Task ConnectServerAsync(long userId, ConnectAction connect, List<string> warnings, CancellationToken ct)
    {
        if (_projects is null)
        {
            warnings.Add("Skipped connect — this bot has no server support.");
            return;
        }
        UserSession session = await _store.GetAsync(userId, ct);
        ServerConnectionInfo? saved = ResolveConnection(connect.Server, session.Connections, out bool ambiguous);
        if (saved is null)
        {
            warnings.Add(ambiguous
                ? $"Skipped connect — \"{Shown(connect.Server)}\" matches several of your connections; use the full URL."
                : $"Skipped connect — \"{Shown(connect.Server)}\" is not a server you saved; connect it first with /connect <url>.");
            return;
        }
        try
        {
            await _projects.ConnectAsync(userId, saved.Url, saved.Token.Length > 0 ? saved.Token : null, saved.VerifyTls, ct);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            warnings.Add($"Skipped connect — {saved.Url} refused: {ex.Message}");
        }
    }

    /// <summary>
    /// §10 <c>disconnect</c>: forget a connection, resolved the same way. A server that
    /// isn't connected is a warning, never a failed plan.
    /// </summary>
    private async Task DisconnectServerAsync(long userId, DisconnectAction disconnect, List<string> warnings, CancellationToken ct)
    {
        if (_projects is null)
        {
            warnings.Add("Skipped disconnect — this bot has no server support.");
            return;
        }
        UserSession session = await _store.GetAsync(userId, ct);
        ServerConnectionInfo? connected = ResolveConnection(disconnect.Server, session.Connections, out bool ambiguous);
        if (connected is null)
        {
            warnings.Add(ambiguous
                ? $"Skipped disconnect — \"{Shown(disconnect.Server)}\" matches several of your connections; use the full URL."
                : $"Skipped disconnect — \"{Shown(disconnect.Server)}\" isn't a connected server.");
            return;
        }
        try
        {
            if (!await _projects.DisconnectAsync(userId, connected.Url, ct))
            {
                warnings.Add($"Skipped disconnect — {connected.Url} isn't a connected server.");
            }
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            warnings.Add($"Skipped disconnect — {connected.Url}: {ex.Message}");
        }
    }

    /// <summary>
    /// §10 server resolution (the browser's <c>resolveServer</c>): exact URL match against the
    /// user's OWN stored connections, else a UNIQUE host[:port]/hostname match; null otherwise
    /// (<paramref name="ambiguous"/> tells the misses apart). No new host, no minted credential.
    /// </summary>
    private static ServerConnectionInfo? ResolveConnection(
        string server, IReadOnlyList<ServerConnectionInfo> connections, out bool ambiguous)
    {
        ambiguous = false;
        string want = server.Trim();
        ServerConnectionInfo? exact = connections.FirstOrDefault(
            c => string.Equals(c.Url, want, StringComparison.OrdinalIgnoreCase));
        if (exact is not null)
        {
            return exact;
        }
        List<ServerConnectionInfo> matches = connections.Where(c =>
            Uri.TryCreate(c.Url, UriKind.Absolute, out Uri? url)
            && (string.Equals(url.Authority, want, StringComparison.OrdinalIgnoreCase)
                || string.Equals(url.Host, want, StringComparison.OrdinalIgnoreCase))).ToList();
        ambiguous = matches.Count > 1;
        return matches.Count == 1 ? matches[0] : null;
    }

    /// <summary>Clip a model-written server string echoed into a warning.</summary>
    private static string Shown(string server)
    {
        string s = server.Trim();
        return s.Length <= MaxLabelChars ? s : s[..MaxLabelChars] + "…";
    }
}
