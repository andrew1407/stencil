using System.Text.Json;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Application.Llm.Plan;

public static partial class OpPlanParser
{
    // §2.1 + §10 as the bot draws the line: a variant or preview holds image edits only, and core lets
    // `describe`/`export` ride there — here they cost the variant (or the preview) instead.
    private static bool staysTopLevel(string op) =>
        Array.IndexOf(OpRegistry.TopLevelOnlyNames, op) >= 0 || Array.IndexOf(OpRegistry.SettingsNames, op) >= 0;

    // Core's result keeps only the variants it did not drop, so each one's 1-based place is recovered
    // from the W_VARIANT_DROPPED indices around it.
    private static List<OpVariant> mapVariants(
        JsonElement list, List<JsonElement> coreWarnings, SortedDictionary<int, string> dropped, List<string> warnings)
    {
        HashSet<int> coreDropped = [.. coreWarnings.Where(w => str(w, "code") == _variantDroppedCode)
            .Select(w => integer(w, "index") ?? 0)];
        List<OpVariant> variants = new();
        int index = 0;
        foreach (JsonElement v in list.EnumerateArray())
        {
            do
            {
                index++;
            }
            while (coreDropped.Contains(index));
            string label = str(v, "label") ?? "";
            List<PlanAction> actions = mapActions(v.GetProperty("actions"), warnings);
            if (actions.FirstOrDefault(a => staysTopLevel(a.Op)) is PlanAction misplacedAction)
            {
                string shown = label.Length > 0 ? label : $"variant {index}";
                dropped[index] = $"Dropped variant {index} (\"{shown}\") — {misplaced(misplacedAction.Op, "variants")}; the rest of the plan ran";
                continue;
            }
            variants.Add(new OpVariant(label, actions));
        }
        return variants;
    }

    // The bot's own drops, in variant order among core's.
    private static void flushDrops(SortedDictionary<int, string> drops, int before, List<string> warnings)
    {
        foreach (int index in drops.Keys.Where(k => k < before).ToList())
        {
            warnings.Add(drops[index]);
            drops.Remove(index);
        }
    }

    // Why the bot drops an op core let ride, framed as core words its own drops.
    private static string misplaced(string op, string scope) =>
        Array.IndexOf(OpRegistry.TopLevelOnlyNames, op) >= 0
            ? $"\"{op}\" is a top-level action only (§2.1) — not allowed inside {scope}"
            : $"\"{op}\" is not an image edit (§10) — not allowed inside {scope}";

    // The first op a preview may not carry, or null when the preview stands.
    private static string? misplacedInPreview(JsonElement option) =>
        option.TryGetProperty("actions", out JsonElement actions) && actions.ValueKind == JsonValueKind.Array
            ? actions.EnumerateArray().Select(a => str(a, "op") ?? "").FirstOrDefault(staysTopLevel)
            : null;
}
