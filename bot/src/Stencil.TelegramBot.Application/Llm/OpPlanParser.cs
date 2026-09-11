using System.Text.Json;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Application.Llm;

/// <summary>
/// The outcome of parsing an LLM reply: a plan (always present on success — a reply with no
/// JSON object at all is a valid <i>chat-only</i> plan), warnings for skipped unknown ops, or
/// a plan-level <see cref="Error"/> (nothing executes; the error is shown in chat).
/// </summary>
public sealed record OpPlanParseResult(OpPlan? Plan, IReadOnlyList<string> Warnings, string? Error);

/// <summary>
/// Op-plan extraction + strict validation, implementing <c>llm-contract.md</c> §1–3 exactly:
/// fences stripped, the first balanced <c>{…}</c> JSON object taken (none ⇒ chat-only); an
/// unknown <c>op</c> drops with a warning, a known op with invalid params fails the WHOLE plan
/// — except §1's leniency: a variant or ask-preview carrying a top-level-only/settings op is
/// dropped alone. Every shape check is table-driven by <see cref="OpSchema"/> (the embedded
/// op registry); the per-op normalizers live in OpPlanParser.Actions.cs.
/// </summary>
public static partial class OpPlanParser
{
    private static readonly OpSchema Schema = OpSchema.Bot;

    // §11 interactive replies — the registry's numbers, the same in every client.
    public static readonly int MaxAskOptions = Schema.Limit("ask.maxOptions");
    public static readonly int MaxAskAnswer = Schema.Limit("ask.answer");
    public static readonly string DefaultCustomLabel = Schema.DefaultCustomLabel;

    /// <summary>Longest value echoed into a warning (so a huge label can't flood the chat).</summary>
    private const int MaxEchoedChars = 40;

    /// <summary>§2/§2.1 top-level-only ops — banned inside variants AND ask previews (§13: registry-derived).</summary>
    private static readonly string[] TopLevelOnlyOps = OpRegistry.TopLevelOnlyNames;

    /// <summary>
    /// The §10-scoped ops the bot carries (§13: registry-derived). Not image edits: banned
    /// inside variants (which exist to produce images) and inside ask-option previews, per
    /// the existing §10 rule.
    /// </summary>
    private static readonly string[] SettingsOps = OpRegistry.SettingsNames;

    /// <summary>
    /// Every op name the parser handles — the bot's registry entries in prompt order. Tests
    /// cross-check this set against <see cref="OpRegistry.Names"/> and against the dispatch
    /// (each name must parse as a KNOWN op, never the §1 unknown-op skip).
    /// </summary>
    public static readonly IReadOnlyList<string> KnownOps =
        Schema.Entries.Select(static e => e.Name).ToArray();

    /// <summary>Validation failure for a known op — fails the whole plan (contract §1).</summary>
    private sealed class PlanException : Exception
    {
        public PlanException(string message) : base(message) { }
    }

    /// <summary>
    /// A top-level-only or settings op found inside a variant / ask-option preview — §1's one
    /// leniency: that variant (or that option's preview) is dropped with a warning, never the
    /// whole plan. The message is the "why" fragment the warning quotes.
    /// </summary>
    private sealed class MisplacedOpException : Exception
    {
        public MisplacedOpException(string message) : base(message) { }
    }

    public static OpPlanParseResult Parse(string? raw)
    {
        string text = (raw ?? "").Trim();
        if (!TryExtractJsonObject(StripFences(text), out JsonDocument? doc))
        {
            // No JSON object at all: the turn is chat-only — the raw text is the reply.
            return new OpPlanParseResult(new OpPlan(text, [], []), [], null);
        }
        using (doc)
        {
            List<string> warnings = new();
            try
            {
                OpPlan plan = ParsePlan(doc!.RootElement, warnings);
                return new OpPlanParseResult(plan, warnings, null);
            }
            catch (Exception ex) when (ex is PlanException or OpSchemaException)
            {
                return new OpPlanParseResult(null, warnings, ex.Message);
            }
        }
    }

    private static OpPlan ParsePlan(JsonElement root, List<string> warnings)
    {
        if (root.ValueKind != JsonValueKind.Object)
        {
            throw new PlanException("the plan is not a JSON object");
        }
        // `version` other than 1 (or absent) is accepted but ignored (contract §1).
        // §1 reply tolerance: models routinely omit the reply while planning valid
        // actions — substitute rather than lose the plan to a missing pleasantry.
        // The substitute itself is chosen below, once the plan's contents are known.
        bool replyOmitted = !root.TryGetProperty("reply", out JsonElement replyElement)
            || replyElement.ValueKind != JsonValueKind.String
            || replyElement.GetString() is not string replyText
            || replyText.Trim().Length == 0;
        string reply = replyOmitted ? string.Empty : replyElement.GetString()!;
        IReadOnlyList<PlanAction> actions = ParseActionList(root, "actions", warnings);
        List<OpVariant> variants = new();
        if (root.TryGetProperty("variants", out JsonElement variantsElement)
            && variantsElement.ValueKind != JsonValueKind.Null)
        {
            Schema.CheckEnvelope(variantsElement, "variants");
            int number = 0;
            foreach (JsonElement variantElement in variantsElement.EnumerateArray())
            {
                number++;
                string label = OptionalString(variantElement, Schema.VariantKeys, "label") ?? "";
                // §1's one leniency: a misplaced op costs THIS variant its place, not the turn.
                // Its own warnings go with it — nothing of it will run.
                List<string> variantWarnings = new();
                try
                {
                    IReadOnlyList<PlanAction> variantActions =
                        ParseActionList(variantElement, "actions", variantWarnings, inVariant: true);
                    warnings.AddRange(variantWarnings);
                    variants.Add(new OpVariant(label, variantActions));
                }
                catch (MisplacedOpException ex)
                {
                    warnings.Add($"Dropped variant {number}{Named(label)} — {ex.Message} and can't ride inside a variant.");
                }
            }
        }
        AskCard? ask = ParseAsk(root, warnings);
        // "Done." only when the plan actually carries work — a bare "Done." on
        // an empty plan reads as a success that never occurred (contract §1).
        if (replyOmitted)
        {
            if (actions.Count > 0 || variants.Count > 0 || ask is not null)
            {
                reply = "Done.";
                warnings.Add("The model omitted its reply — the plan still ran");
            }
            else
            {
                reply = "The model returned an empty plan — nothing was changed.";
            }
        }
        return new OpPlan(reply, actions, variants, ask);
    }
}
