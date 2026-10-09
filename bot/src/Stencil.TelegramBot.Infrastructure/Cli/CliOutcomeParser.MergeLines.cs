using System.Text.Json;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Serialization;

namespace Stencil.TelegramBot.Infrastructure.Cli;

public static partial class CliOutcomeParser
{
    private const string _notALineMerge = "the stencil CLI did not return a line merge";

    // The one --merge-lines document version this adapter reads.
    public const int MERGE_LINES_VERSION = 1;

    // {"version":1,"lines":[…],"peerAdded":bool} on stdout: the peer's lines, then the kept local ones.
    public static LineMerge ParseMergeLines(string stdout)
    {
        try
        {
            using JsonDocument doc = JsonDocument.Parse(stdout);
            JsonElement root = doc.RootElement;
            if (root.ValueKind != JsonValueKind.Object
                || !root.TryGetProperty("lines", out JsonElement lines)
                || lines.ValueKind != JsonValueKind.Array)
            {
                throw new StencilCliException(_notALineMerge);
            }
            if (number(root, "version") != MERGE_LINES_VERSION)
            {
                throw new StencilCliException($"the stencil CLI speaks merge-lines version {number(root, "version")}, not {MERGE_LINES_VERSION}");
            }
            bool peerAdded = root.TryGetProperty("peerAdded", out JsonElement added) && added.ValueKind == JsonValueKind.True;
            List<LayoutLine?> merged = StencilJson.FromElement<List<LayoutLine?>>(lines) ?? [];
            return new LineMerge([.. merged.OfType<LayoutLine>()], peerAdded);
        }
        catch (JsonException)
        {
            throw new StencilCliException(_notALineMerge);
        }
    }
}
