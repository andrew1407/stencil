using System.Text.Json;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Application.Llm.Schema;

namespace Stencil.TelegramBot.Application.Llm.Plan;

// A reply with no JSON object at all is a valid chat-only plan; an Error means nothing executes.
public sealed record OpPlanParseResult(OpPlan? Plan, IReadOnlyList<string> Warnings, string? Error);

// §1–3 exactly: fences stripped, the first balanced {…} taken (none ⇒ chat-only); an unknown op drops
// with a warning, a known op with invalid params fails the WHOLE plan — except §1's variant leniency.
public static partial class OpPlanParser
{
    private static readonly OpSchema _schema = OpSchema.Bot;

    // §11 interactive replies — the registry's numbers, the same in every client.
    public static readonly int MaxAskOptions = _schema.Limit("ask.maxOptions");
    public static readonly int MaxAskAnswer = _schema.Limit("ask.answer");
    public static readonly string DefaultCustomLabel = _schema.DefaultCustomLabel;

    private const int _maxEchoedChars = 40;

    // Banned inside variants AND ask previews (§13: registry-derived).
    private static readonly string[] _topLevelOnlyOps = OpRegistry.TopLevelOnlyNames;

    // §10 settings ops: not image edits, banned inside variants and ask previews.
    private static readonly string[] _settingsOps = OpRegistry.SettingsNames;

    // Tests cross-check this against OpRegistry.Names and the dispatch.
    public static readonly IReadOnlyList<string> KnownOps =
        _schema.Entries.Select(static e => e.Name).ToArray();

    private sealed class PlanException : Exception
    {
        public PlanException(string message) : base(message) { }
    }

    // §1's one leniency: the variant (or option preview) is dropped with a warning, never the whole
    // plan.
    private sealed class MisplacedOpException : Exception
    {
        public MisplacedOpException(string message) : base(message) { }
    }

    public static OpPlanParseResult Parse(string? raw)
    {
        string text = (raw ?? "").Trim();
        if (!tryExtractJsonObject(stripFences(text), out JsonDocument? doc))
        {
            // No JSON object at all: the turn is chat-only — the raw text is the reply.
            return new OpPlanParseResult(new OpPlan(text, [], []), [], null);
        }
        using (doc)
        {
            List<string> warnings = new();
            try
            {
                OpPlan plan = parsePlan(doc!.RootElement, warnings);
                return new OpPlanParseResult(plan, warnings, null);
            }
            catch (Exception ex) when (ex is PlanException or OpSchemaException)
            {
                return new OpPlanParseResult(null, warnings, ex.Message);
            }
        }
    }

    private static OpPlan parsePlan(JsonElement root, List<string> warnings)
    {
        if (root.ValueKind != JsonValueKind.Object)
        {
            throw new PlanException("the plan is not a JSON object");
        }
        // A version other than 1 is accepted but ignored (§1). A missing reply is substituted rather than
        // losing the plan over a missing pleasantry.
        bool replyOmitted = !root.TryGetProperty("reply", out JsonElement replyElement)
            || replyElement.ValueKind != JsonValueKind.String
            || replyElement.GetString() is not string replyText
            || replyText.Trim().Length == 0;
        string reply = replyOmitted ? string.Empty : replyElement.GetString()!;
        IReadOnlyList<PlanAction> actions = parseActionList(root, "actions", warnings);
        List<OpVariant> variants = new();
        if (root.TryGetProperty("variants", out JsonElement variantsElement)
            && variantsElement.ValueKind != JsonValueKind.Null)
        {
            _schema.CheckEnvelope(variantsElement, "variants");
            int number = 0;
            foreach (JsonElement variantElement in variantsElement.EnumerateArray())
            {
                number++;
                string label = optionalString(variantElement, _schema.VariantKeys, "label") ?? "";
                // §1's one leniency: a misplaced op costs THIS variant its place, warnings
                // included.
                List<string> variantWarnings = new();
                try
                {
                    IReadOnlyList<PlanAction> variantActions =
                        parseActionList(variantElement, "actions", variantWarnings, inVariant: true);
                    warnings.AddRange(variantWarnings);
                    variants.Add(new OpVariant(label, variantActions));
                }
                catch (MisplacedOpException ex)
                {
                    warnings.Add($"Dropped variant {number}{named(label)} — {ex.Message} and can't ride inside a variant.");
                }
            }
        }
        AskCard? ask = parseAsk(root, warnings);
        // "Done." only when the plan carries work — on an empty plan it reads as a success that
        // never occurred.
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
