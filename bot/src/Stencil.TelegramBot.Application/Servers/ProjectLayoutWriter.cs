using System.Text.Json;
using System.Text.Json.Nodes;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Serialization;

namespace Stencil.TelegramBot.Application.Servers;

/// <summary>
/// Builds the project layout JSON to save back to the server. The counterpart of
/// <see cref="ProjectLayoutMapper"/>: it starts from the project's existing layout (so fields
/// the bot doesn't model — <c>cropRect</c>, formulas — are preserved) and overwrites the
/// fields the bot owns from the current <see cref="EditState"/>: <c>lines</c>,
/// <c>imageFilter</c>/<c>filterColor</c>, <c>rotationQuarters</c>, and the working dimensions.
/// <c>pageSize</c> is preserved too unless the user picked one with <c>/format</c>
/// (<see cref="EditState.PageFormat"/>), which then overrides it.
/// </summary>
/// <remarks>
/// Pure (no I/O), so it's unit-tested. The result matches the browser's <c>buildLayoutPayload</c>
/// shape, so a browser/desktop client reopening a bot-saved project reconstructs the same result.
/// </remarks>
public static class ProjectLayoutWriter
{
    /// <summary>
    /// Merge the current edit state into <paramref name="baseLayoutJson"/> (the fetched layout,
    /// or null for a bot-created project). <paramref name="resultWidth"/>/<paramref name="resultHeight"/>
    /// are the rendered result dimensions (the working-image size the browser records).
    /// </summary>
    public static JsonObject Build(string? baseLayoutJson, EditState edits, int resultWidth, int resultHeight)
    {
        JsonObject root = TryParseObject(baseLayoutJson) ?? new JsonObject();

        root["lines"] = JsonSerializer.SerializeToNode(edits.Layout?.Lines ?? [], StencilJson.Options);

        var (mode, color) = FilterFields(edits.Filter);
        root["imageFilter"] = mode;
        if (color is not null)
        {
            root["filterColor"] = color;
        }

        // A /format choice overrides the fetched layout's page; otherwise it is preserved as-is.
        if (edits.PageFormat is string page)
        {
            root["pageSize"] = page;
            if (page == "custom" && edits.CustomPageWidth is double pw && edits.CustomPageHeight is double ph)
            {
                root["customPageWidth"] = pw;
                root["customPageHeight"] = ph;
            }
        }

        root["rotationQuarters"] = edits.Rotate;
        root["imageWidth"] = resultWidth;
        root["imageHeight"] = resultHeight;
        return root;
    }

    /// <summary>Serialize <see cref="Build"/>'s result to a compact JSON string.</summary>
    public static string BuildJson(string? baseLayoutJson, EditState edits, int resultWidth, int resultHeight) =>
        Build(baseLayoutJson, edits, resultWidth, resultHeight).ToJsonString();

    /// <summary>
    /// Map the bot's filter spec to the browser's <c>imageFilter</c>/<c>filterColor</c> pair:
    /// <c>bw</c>/<c>sepia</c>/<c>invert</c>/<c>contour</c> stay named; any colour becomes a
    /// <c>custom</c> tint; null is <c>none</c>.
    /// </summary>
    private static (string Mode, string? Color) FilterFields(string? filter)
    {
        if (string.IsNullOrEmpty(filter))
        {
            return ("none", null);
        }
        if (filter.Equals("bw", StringComparison.OrdinalIgnoreCase))
        {
            return ("bw", null);
        }
        if (filter.Equals("sepia", StringComparison.OrdinalIgnoreCase))
        {
            return ("sepia", null);
        }
        if (filter.Equals("invert", StringComparison.OrdinalIgnoreCase))
        {
            return ("invert", null);
        }
        if (filter.Equals("contour", StringComparison.OrdinalIgnoreCase))
        {
            return ("contour", null);
        }
        return ("custom", filter);
    }

    private static JsonObject? TryParseObject(string? json)
    {
        if (string.IsNullOrWhiteSpace(json))
        {
            return null;
        }
        try
        {
            return JsonNode.Parse(json) as JsonObject;
        }
        catch (JsonException)
        {
            return null;
        }
    }
}
