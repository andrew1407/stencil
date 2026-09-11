using System.Text.Json;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Application.Llm;

// OpPlanParser — §2 action lists: the envelope bound, the registry lookup that drops an unknown
// op with a warning, and §2.1's top-level-only ban. Class doc lives in OpPlanParser.cs.
public static partial class OpPlanParser
{
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
