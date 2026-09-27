using System.Text.Json;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Application.Llm.Plan;

// A reply with no JSON object at all is a valid chat-only plan; an Error means nothing executes.
public sealed record OpPlanParseResult(OpPlan? Plan, IReadOnlyList<string> Warnings, string? Error);

// The typed mapper over core's verdict (`stencil --plan-check`, cli/CONTRACT.md §7): core extracts,
// validates and normalizes under llm-contract.md §1–§3; this maps its result onto PlanActions and
// shows every failure and warning in core's canonical `message`.
public static partial class OpPlanParser
{
    public const string STATUS_INVALID = "invalid";

    private const string _variantDroppedCode = "W_VARIANT_DROPPED";
    private const string _previewDroppedCode = "W_PREVIEW_DROPPED";
    private const string _replyOmittedCode = "W_REPLY_OMITTED";

    // §11 interactive replies — the registry's numbers, the same in every client.
    public static readonly int MaxAskOptions = OpRegistryAsset.AskMaxOptions;
    public static readonly int MaxAskAnswer = OpRegistryAsset.AskMaxAnswer;
    public static readonly string DefaultCustomLabel = OpRegistryAsset.DefaultCustomLabel;

    // Tests cross-check this against OpRegistry.Names and the dispatch.
    public static readonly IReadOnlyList<string> KnownOps = [.. OpRegistryAsset.Ops.Select(static o => o.Name)];

    private sealed class PlanException : Exception
    {
        public PlanException(string message) : base(message) { }
    }

    public static async Task<OpPlanParseResult> ParseAsync(IStencilCli cli, string? raw, CancellationToken ct = default) =>
        Map((await cli.PlanCheckAsync(raw ?? "", ct).ConfigureAwait(false)).Result);

    // A `--script-plan` chunk carries no reply by design, so core's reply-omitted note is not news.
    public static OpPlanParseResult MapScriptChunk(string result) => Map(result, replyExpected: false);

    // A result that breaks cli/CONTRACT.md §7 is the CLI's fault, reported like any failed run.
    public static OpPlanParseResult Map(string result, bool replyExpected = true)
    {
        try
        {
            return mapResult(result, replyExpected);
        }
        catch (Exception ex) when (ex is JsonException or KeyNotFoundException or InvalidOperationException or FormatException)
        {
            throw new StencilCliException("the stencil CLI returned a plan check this bot cannot read");
        }
    }

    private static OpPlanParseResult mapResult(string result, bool replyExpected)
    {
        using JsonDocument doc = JsonDocument.Parse(result);
        JsonElement r = doc.RootElement;
        if (str(r, "status") == STATUS_INVALID)
        {
            return new OpPlanParseResult(null, [], message(r.GetProperty("error")));
        }
        try
        {
            List<string> warnings = new();
            List<PlanAction> actions = mapActions(r.GetProperty("actions"), warnings);
            List<JsonElement> coreWarnings = [.. r.GetProperty("warnings").EnumerateArray()];
            SortedDictionary<int, string> ownDrops = new();
            List<OpVariant> variants = mapVariants(r.GetProperty("variants"), coreWarnings, ownDrops, warnings);
            Dictionary<int, string> droppedPreviews = new();
            string? replyOmitted = null;
            foreach (JsonElement w in coreWarnings)
            {
                switch (str(w, "code"))
                {
                    case _variantDroppedCode:
                        flushDrops(ownDrops, integer(w, "index") ?? 0, warnings);
                        warnings.Add(message(w));
                        break;
                    case _previewDroppedCode: droppedPreviews[integer(w, "index") ?? 0] = message(w); break;
                    case _replyOmittedCode: replyOmitted = message(w); break;
                    default: warnings.Add(message(w)); break;
                }
            }
            flushDrops(ownDrops, int.MaxValue, warnings);
            AskCard? ask = mapAsk(r, droppedPreviews, warnings);
            if (replyOmitted is not null && replyExpected)
            {
                warnings.Add(replyOmitted);
            }
            return new OpPlanParseResult(new OpPlan(str(r, "reply") ?? "", actions, variants, ask), warnings, null);
        }
        catch (PlanException ex)
        {
            return new OpPlanParseResult(null, [], ex.Message);
        }
    }

    // Null when the CLI judges with the registry the bot was built with.
    public static async Task<string?> RegistrySkewAsync(IStencilCli cli, CancellationToken ct = default)
    {
        PlanCheck check = await cli.PlanCheckAsync("", ct).ConfigureAwait(false);
        return OpRegistryAsset.Matches(check) ? null : OpRegistryAsset.Describe(check);
    }

    private static List<PlanAction> mapActions(JsonElement list, List<string> warnings)
    {
        List<PlanAction> actions = new();
        foreach (JsonElement a in list.EnumerateArray())
        {
            string op = str(a, "op") ?? "";
            if (!_mappers.TryGetValue(op, out Func<JsonElement, PlanAction>? map))
            {
                // A CLI built from a newer registry: §1's forward-compatible skip, in core's words.
                warnings.Add($"Skipped unknown operation \"{op}\"");
                continue;
            }
            try
            {
                actions.Add(map(a));
            }
            catch (PlanException ex)
            {
                throw new PlanException($"Invalid {op} action: {ex.Message}");
            }
        }
        return actions;
    }
}
