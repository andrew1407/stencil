using System.Text.Json;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Infrastructure.Cli;

public static partial class CliOutcomeParser
{
    private const string _notAScriptPlan = "the stencil CLI did not return a script plan";

    // cli/CONTRACT.md §5: `--script-plan` prints one JSON object to STDOUT, one trailing newline, nothing
    // else. The envelope's `script` label is dropped: it is this adapter's temp leaf, not a user's name.
    public static ScriptPlan ParseScriptPlan(string stdout)
    {
        using JsonDocument doc = parseEnvelope(stdout);
        JsonElement root = doc.RootElement;
        if (root.ValueKind != JsonValueKind.Object)
        {
            throw new StencilCliException(_notAScriptPlan);
        }
        return new ScriptPlan(readDiagnostics(root), readBlocks(root));
    }

    private static JsonDocument parseEnvelope(string stdout)
    {
        try
        {
            return JsonDocument.Parse(stdout);
        }
        catch (JsonException)
        {
            throw new StencilCliException(_notAScriptPlan);
        }
    }

    private static IReadOnlyList<ScriptDiagnostic> readDiagnostics(JsonElement root)
    {
        List<ScriptDiagnostic> diagnostics = new();
        foreach (JsonElement d in array(root, "diagnostics"))
        {
            diagnostics.Add(new ScriptDiagnostic(
                text(d, "severity", ScriptDiagnostic.SEVERITY_ERROR),
                text(d, "code", ""),
                number(d, "line"),
                number(d, "col"),
                text(d, "message", "")));
        }
        return diagnostics;
    }

    private static IReadOnlyList<ScriptBlock> readBlocks(JsonElement root)
    {
        List<ScriptBlock> blocks = new();
        foreach (JsonElement b in array(root, "blocks"))
        {
            List<string> plans = new();
            foreach (JsonElement plan in array(b, "plans"))
            {
                if (plan.TryGetProperty("actions", out JsonElement actions)
                    && actions.ValueKind == JsonValueKind.Array)
                {
                    plans.Add(actions.GetRawText());
                }
            }
            blocks.Add(new ScriptBlock(
                number(b, "index"),
                text(b, "source", ""),
                text(b, "sourceKind", ScriptBlock.KIND_PROJECT),
                plans));
        }
        return blocks;
    }

    private static IEnumerable<JsonElement> array(JsonElement parent, string name) =>
        parent.TryGetProperty(name, out JsonElement value) && value.ValueKind == JsonValueKind.Array
            ? value.EnumerateArray()
            : [];

    private static string text(JsonElement parent, string name, string fallback) =>
        parent.TryGetProperty(name, out JsonElement value) && value.ValueKind == JsonValueKind.String
            ? value.GetString()!
            : fallback;

    private static int number(JsonElement parent, string name) =>
        parent.TryGetProperty(name, out JsonElement value)
        && value.ValueKind == JsonValueKind.Number
        && value.TryGetInt32(out int parsed)
            ? parsed
            : 0;
}
