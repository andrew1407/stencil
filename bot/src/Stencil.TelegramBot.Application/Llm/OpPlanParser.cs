using System.Text;
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

    private static string StripFences(string text)
    {
        if (!text.Contains("```"))
        {
            return text;
        }
        StringBuilder sb = new(text.Length);
        foreach (string line in text.Split('\n'))
        {
            if (line.TrimStart().StartsWith("```", StringComparison.Ordinal))
            {
                continue;
            }
            sb.Append(line).Append('\n');
        }
        return sb.ToString();
    }

    /// <summary>
    /// Find the first balanced <c>{…}</c> substring that parses as JSON (string/escape-aware
    /// brace matching, retrying from each later <c>{</c> so stray braces in prose don't hide a
    /// real object).
    /// </summary>
    private static bool TryExtractJsonObject(string text, out JsonDocument? doc)
    {
        for (int start = text.IndexOf('{'); start >= 0; start = text.IndexOf('{', start + 1))
        {
            if (!TryFindBalancedEnd(text, start, out int end))
            {
                continue;
            }
            try
            {
                doc = JsonDocument.Parse(text[start..(end + 1)]);
                return true;
            }
            catch (JsonException)
            {
                // Balanced but not JSON (e.g. braces in prose) — try the next candidate.
            }
        }
        doc = null;
        return false;
    }

    private static bool TryFindBalancedEnd(string text, int start, out int end)
    {
        int depth = 0;
        bool inString = false;
        for (int i = start; i < text.Length; i++)
        {
            char c = text[i];
            if (inString)
            {
                if (c == '\\')
                {
                    i++; // skip the escaped character
                }
                else if (c == '"')
                {
                    inString = false;
                }
                continue;
            }
            switch (c)
            {
                case '"': inString = true; break;
                case '{': depth++; break;
                case '}':
                    depth--;
                    if (depth == 0)
                    {
                        end = i;
                        return true;
                    }
                    break;
            }
        }
        end = -1;
        return false;
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

    /// <summary>
    /// Parse the optional <c>ask</c> object (contract §11): its structure is the registry's
    /// ask schema (question, 2..5 options, the free-text row, an option's exactly-one-of image
    /// reference); the preview actions are ordinary §2 actions validated "inside a preview".
    /// A card nobody can answer rejects the whole plan rather than reaching the user broken.
    /// </summary>
    private static AskCard? ParseAsk(JsonElement root, List<string> warnings)
    {
        if (!root.TryGetProperty("ask", out JsonElement ask) || ask.ValueKind == JsonValueKind.Null)
        {
            return null;
        }
        Schema.ValidateAsk(ask);
        JsonElement keys = Schema.AskKeys;
        string question = OptionalString(ask, keys, "question")!;
        bool multi = OptionalString(ask, keys, "mode") == "multi";
        bool allowCustom = OptionalBool(ask, "allowCustom") ?? false;
        string customLabel = OptionalString(ask, keys, "customLabel") ?? DefaultCustomLabel;
        JsonElement optionKeys = keys.GetProperty("options").GetProperty("items").GetProperty("fields");

        List<AskOption> options = new();
        bool droppedPreview = false;
        int index = 0;
        foreach (JsonElement optionElement in ask.GetProperty("options").EnumerateArray())
        {
            index++;
            string label = OptionalString(optionElement, optionKeys, "label")!;
            if (HasField(optionElement, "image"))
            {
                NoteAskImage(optionElement.GetProperty("image"), index, warnings);
            }
            if (HasField(optionElement, "actions"))
            {
                // §1: a preview asking for a top-level-only or settings op loses the PREVIEW
                // and says which option and why — the option, and the plan, stand. This chat
                // renders no previews, so a well-formed one is dropped too (with its own notes).
                try
                {
                    ParseActionList(optionElement, "actions", new List<string>(), inVariant: true);
                    droppedPreview = true;
                }
                catch (MisplacedOpException ex)
                {
                    warnings.Add($"Dropped the preview for option {index}{Named(label)} — {ex.Message} and can't ride inside an ask option's preview.");
                }
            }
            options.Add(new AskOption(label));
        }
        if (droppedPreview)
        {
            warnings.Add("some options asked for a rendered preview, which this chat can't produce — they are listed by name");
        }
        return new AskCard(question, multi, allowCustom, customLabel, options);
    }

    /// <summary>
    /// Warn that the bot shows no picture for an option's (already validated) <c>image</c>
    /// reference (§11.2). None resolves here: a <c>url</c> would have Telegram fetch a host
    /// nobody chose.
    /// </summary>
    private static void NoteAskImage(JsonElement image, int index, List<string> warnings)
    {
        warnings.Add(HasField(image, "scanIndex")
            ? $"ask option {index} names a page-scan image, which this chat has no access to"
            : HasField(image, "projectId")
                ? $"ask option {index} names a stored project, which this chat can't preview"
                : $"ask option {index} names an image URL, which this chat does not fetch — the option is shown without a preview");
    }

    /// <summary>
    /// The text an answered card sends as the user's next turn: the picked labels joined, or the
    /// typed custom text, trimmed and capped (contract §11.3).
    /// </summary>
    public static string AskAnswerText(IEnumerable<string> pickedLabels, string? custom = null)
    {
        string typed = (custom ?? "").Trim();
        string answer = typed.Length > 0
            ? typed
            : string.Join(", ", pickedLabels.Where(static l => !string.IsNullOrWhiteSpace(l)).Select(static l => l.Trim()));
        return answer.Length > MaxAskAnswer ? answer[..MaxAskAnswer] : answer;
    }

    /// <summary>
    /// Parse an optional actions array (missing/null = none), bounded by the registry envelope.
    /// <paramref name="inVariant"/> enforces §2.1's top-level-only ops.
    /// </summary>
    private static IReadOnlyList<PlanAction> ParseActionList(
        JsonElement parent, string name, List<string> warnings, bool inVariant = false)
    {
        if (!parent.TryGetProperty(name, out JsonElement array) || array.ValueKind == JsonValueKind.Null)
        {
            return [];
        }
        Schema.CheckEnvelope(array, "actions");
        List<PlanAction> actions = new();
        foreach (JsonElement element in array.EnumerateArray())
        {
            PlanAction? action = ParseAction(element, warnings, inVariant);
            if (action is not null)
            {
                actions.Add(action);
            }
        }
        return actions;
    }

    /// <summary>One action: null when it carried an unknown op (skipped with a warning).</summary>
    private static PlanAction? ParseAction(JsonElement element, List<string> warnings, bool inVariant = false)
    {
        if (element.ValueKind != JsonValueKind.Object)
        {
            throw new PlanException("each action must be a JSON object");
        }
        if (!element.TryGetProperty("op", out JsonElement opElement)
            || opElement.ValueKind != JsonValueKind.String
            || opElement.GetString() is not string op
            || op.Length == 0)
        {
            throw new PlanException("an action is missing its \"op\" field");
        }
        // §2/§2.1: image/save/undo/redo/reset are top-level only — a variant exists to yield
        // one more take OF the working image, never to switch, persist or rewind one.
        if (inVariant && Array.IndexOf(TopLevelOnlyOps, op) >= 0)
        {
            throw new MisplacedOpException($"\"{op}\" is a top-level action only (§2.1)");
        }
        // §10: settings/connection ops are not image edits — they cost the variant its place.
        if (inVariant && Array.IndexOf(SettingsOps, op) >= 0)
        {
            throw new MisplacedOpException($"\"{op}\" is not an image edit (§10)");
        }
        if (!Schema.Ops.TryGetValue(op, out OpEntry? entry))
        {
            // Forward compatibility: an unknown op is dropped, not fatal (contract §1). A
            // §13 forbidden name lands here too — the executor refuses it if one ever parses.
            warnings.Add($"Skipped an unknown operation \"{op}\".");
            return null;
        }
        try
        {
            return Normalize(Schema.ValidateAction(element, entry), entry);
        }
        catch (PlanException ex)
        {
            throw new PlanException($"invalid \"{op}\" action: {ex.Message}");
        }
    }

    private static bool HasField(JsonElement element, string name) =>
        element.TryGetProperty(name, out JsonElement value) && value.ValueKind != JsonValueKind.Null;

    private static string Named(string label) =>
        label.Trim() is { Length: > 0 } name ? $" (\"{Truncate(name)}\")" : "";

    private static string Truncate(string value) =>
        value.Length <= MaxEchoedChars ? value : value[..MaxEchoedChars] + "…";
}
