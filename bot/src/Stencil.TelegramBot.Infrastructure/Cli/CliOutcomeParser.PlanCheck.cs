using System.Text.Json;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Infrastructure.Cli;

public static partial class CliOutcomeParser
{
    private const string _notAPlanCheck = "the stencil CLI did not return a plan check";

    // The one envelope version this adapter reads; the CLI bumps it only when a consumer must change.
    public const int PLAN_CHECK_VERSION = 1;

    // cli/CONTRACT.md §7: one JSON document on stdout. The verdict stays raw for the typed mapper.
    public static PlanCheck ParsePlanCheck(string stdout)
    {
        JsonDocument doc;
        try
        {
            doc = JsonDocument.Parse(stdout);
        }
        catch (JsonException)
        {
            throw new StencilCliException(_notAPlanCheck);
        }
        using (doc)
        {
            JsonElement root = doc.RootElement;
            if (root.ValueKind != JsonValueKind.Object
                || !root.TryGetProperty("result", out JsonElement result)
                || result.ValueKind != JsonValueKind.Object)
            {
                throw new StencilCliException(_notAPlanCheck);
            }
            if (number(root, "version") != PLAN_CHECK_VERSION)
            {
                throw new StencilCliException($"the stencil CLI speaks plan-check envelope version {number(root, "version")}, not {PLAN_CHECK_VERSION}");
            }
            long bytes = root.TryGetProperty("registryBytes", out JsonElement b) && b.TryGetInt64(out long n) ? n : 0;
            return new PlanCheck(bytes, text(root, "registryFnv1a64", ""), result.GetRawText());
        }
    }
}
