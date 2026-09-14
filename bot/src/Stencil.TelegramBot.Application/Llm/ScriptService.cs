using System.Text.Json;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Llm;

// `/script` and the `.stc` upload: the CLI lowers the script to op-plan JSON and every action runs
// through the SAME validator, pre-flight and executor a model plan does. A script is user text, not
// a second execution path.
public sealed class ScriptService : IScriptService
{
    // Far above any hand-written script and far under IBotPolicy.MaxDocumentBytes.
    public const int MAX_SCRIPT_CHARS = 64 * 1024;

    // The .stc upload's cap, on the bytes it arrives as: UTF-8 never spends fewer bytes than
    // characters, so a file inside it is inside MAX_SCRIPT_CHARS once decoded.
    public const int MAX_SCRIPT_BYTES = MAX_SCRIPT_CHARS;

    // The wrapper plan needs SOME reply: OpPlanParser substitutes one and warns on an empty
    // string. It is never shown — ScriptOutcome carries the reply and PromptOutcome.Applied the
    // verdict.
    private const string _blockReply = "Script block applied.";

    private const int _maxReported = 3;
    private const int _maxEchoedChars = 60;

    private readonly IStencilCli _cli;
    private readonly IUserWorkspace _workspace;
    private readonly ISessionStore _store;
    private readonly IEditingService _editing;
    private readonly PromptService _prompts;

    public ScriptService(
        IStencilCli cli,
        IUserWorkspace workspace,
        ISessionStore store,
        IEditingService editing,
        PromptService prompts)
    {
        _cli = cli;
        _workspace = workspace;
        _store = store;
        _editing = editing;
        _prompts = prompts;
    }

    public async Task<ScriptOutcome> RunAsync(long userId, string scriptText, CancellationToken ct = default)
    {
        if (scriptText.Trim().Length == 0)
        {
            return new ScriptOutcome("The script is empty — send some @directives to run.", [], []);
        }
        if (scriptText.Length > MAX_SCRIPT_CHARS)
        {
            return new ScriptOutcome(
                $"That script is too long — the limit is {MAX_SCRIPT_CHARS / 1024} KB.", [], []);
        }
        string path = Path.Combine(_workspace.DirectoryFor(userId), $"script-{Guid.NewGuid():N}.stc");
        try
        {
            await File.WriteAllTextAsync(path, scriptText, ct);
            ScriptPlan plan = await _cli.ScriptPlanAsync(path, await frameAsync(userId, ct), ct);
            return await applyAsync(userId, scriptText, plan, ct);
        }
        finally
        {
            try { File.Delete(path); } catch (IOException) { } catch (UnauthorizedAccessException) { }
        }
    }

    // The frame a script's % lengths resolve against is the one the user is looking at — which is
    // also the frame PlanFrameMapper maps the resulting coordinates back from. The CLI only
    // header-probes it, so whenever the edits leave the size alone the base image is that frame
    // and no render is spawned.
    private async Task<string?> frameAsync(long userId, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(userId, ct);
        if (session.OriginalImagePath is not string original)
        {
            return null;
        }
        PlanFrameMapper frame = PlanFrameMapper.ForSession(session);
        if (frame.Width == session.OriginalWidth && frame.Height == session.OriginalHeight)
        {
            return original;
        }
        try
        {
            return (await _editing.RenderAsync(userId, ct)).Path;
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            return null;   // no frame → the CLI drops the shape ops rather than guessing a size
        }
    }

    private async Task<ScriptOutcome> applyAsync(
        long userId, string scriptText, ScriptPlan script, CancellationToken ct)
    {
        List<string> warnings = script.Warnings.Select(static d => d.ToString()).ToList();
        if (script.HasErrors)
        {
            return new ScriptOutcome(errorReply(script), warnings, []);
        }
        List<PromptRender> renders = new();
        // Several blocks each produce their own picture, so they come back as an album instead of
        // the single working-image render.
        bool album = script.Blocks.Count > 1;
        bool mutated = false;
        int applied = 0;
        foreach (ScriptBlock block in script.Blocks)
        {
            if (block.IsLocalSource)
            {
                return new ScriptOutcome(localSourceReply(block), warnings, renders, mutated && !album);
            }
            bool blockMutated = false;
            foreach (string actions in block.Plans)
            {
                OpPlanParseResult parsed = OpPlanParser.Parse(planFor(actions));
                warnings.AddRange(parsed.Warnings);
                if (parsed.Plan is not OpPlan plan)
                {
                    return new ScriptOutcome(
                        $"The script lowered to something this bot can't run — {parsed.Error}. Nothing else was applied.",
                        warnings, renders, (mutated || blockMutated) && !album);
                }
                PromptOutcome outcome = await _prompts.RunPlanAsync(userId, plan, [], scriptText, ct);
                warnings.AddRange(outcome.Warnings);
                renders.AddRange(outcome.Renders);
                blockMutated |= outcome.Mutated;
                if (!outcome.Applied)
                {
                    return new ScriptOutcome(outcome.Reply, warnings, renders, (mutated || blockMutated) && !album);
                }
                applied += plan.Actions.Count;
            }
            mutated |= blockMutated;
            if (album && blockMutated)
            {
                renders.Add(new PromptRender(
                    await labelAsync(userId, block, ct), await _editing.RenderAsync(userId, ct)));
            }
        }
        return new ScriptOutcome(
            $"Script ran: {applied} op{(applied == 1 ? "" : "s")}.", warnings, renders, mutated && !album);
    }

    // The executor never sees the envelope, only a plan object, so the actions array is wrapped in
    // one.
    private static string planFor(string actionsJson) =>
        $"{{\"reply\":{JsonSerializer.Serialize(_blockReply)},\"actions\":{actionsJson}}}";

    private static string errorReply(ScriptPlan script)
    {
        string[] reported = script.Errors.Take(_maxReported).Select(static d => d.ToString()).ToArray();
        int more = script.Errors.Count() - reported.Length;
        return "The script has errors, so nothing ran:\n" + string.Join("\n", reported)
            + (more > 0 ? $"\n…and {more} more." : "");
    }

    private static string localSourceReply(ScriptBlock block) =>
        $"Block {block.Index + 1} reads \"{shorten(block.Source)}\" off a file system this bot has no "
        + "access to. Use an http(s) link in @source, or drop the @source line to edit the photo you sent.";

    private async Task<string> labelAsync(long userId, ScriptBlock block, CancellationToken ct)
    {
        if (block.SourceKind == ScriptBlock.KIND_URL && block.Source.Length > 0)
        {
            return shorten(block.Source);
        }
        UserSession session = await _store.GetAsync(userId, ct);
        return session.ImageLabel ?? "image";
    }

    private static string shorten(string value) =>
        value.Length <= _maxEchoedChars ? value : value[.._maxEchoedChars] + "…";
}
