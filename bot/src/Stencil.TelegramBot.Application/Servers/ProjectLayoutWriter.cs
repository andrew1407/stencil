using System.Text.Json;
using System.Text.Json.Nodes;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Serialization;

namespace Stencil.TelegramBot.Application.Servers;

// The counterpart of ProjectLayoutMapper: starts from the project's existing layout so fields the bot
// doesn't model (cropRect, formulas) survive; pageSize is overwritten only after a /format pick.
public static class ProjectLayoutWriter
{
    // resultWidth/Height are the rendered result dimensions (the working-image size the browser
    // records).
    public static JsonObject Build(string? baseLayoutJson, EditState edits, int resultWidth, int resultHeight)
    {
        JsonObject root = tryParseObject(baseLayoutJson) ?? new JsonObject();
        normalizeCropRect(root);

        root["lines"] = JsonSerializer.SerializeToNode(edits.Layout?.Lines ?? [], StencilJson.Options);

        var (mode, color) = filterFields(edits.Filter);
        root["imageFilter"] = mode;
        if (color is not null)
        {
            root["filterColor"] = color;
        }

        if (edits.PageFormat is string page)
        {
            root["pageSize"] = page;
            if (page == "custom" && edits.CustomPageWidth is double pw && edits.CustomPageHeight is double ph)
            {
                root["customPageWidth"] = pw;
                root["customPageHeight"] = ph;
            }
        }

        // An LLM formula op overrides that axis; otherwise the base layout's formula is preserved.
        if (edits.FormulaX is string formulaX)
        {
            root["formulaX"] = formulaX;
            root["allowFormulas"] = true;
        }
        if (edits.FormulaY is string formulaY)
        {
            root["formulaY"] = formulaY;
            root["allowFormulas"] = true;
        }

        root["rotationQuarters"] = edits.Rotate;
        root["imageWidth"] = resultWidth;
        root["imageHeight"] = resultHeight;
        return root;
    }

    public static string BuildJson(string? baseLayoutJson, EditState edits, int resultWidth, int resultHeight) =>
        Build(baseLayoutJson, edits, resultWidth, resultHeight).ToJsonString();

    // bw/sepia/invert/contour stay named; any colour becomes a custom tint; null is none.
    private static (string Mode, string? Color) filterFields(string? filter)
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

    // A preserved legacy cropRect ({width,height}) is rewritten to the canonical {x,y,w,h} keys.
    private static void normalizeCropRect(JsonObject root)
    {
        if (root["cropRect"] is not JsonObject rect)
        {
            return;
        }
        foreach (var (legacy, canonical) in new[] { ("width", "w"), ("height", "h") })
        {
            if (rect.ContainsKey(legacy))
            {
                if (!rect.ContainsKey(canonical))
                {
                    rect[canonical] = rect[legacy]?.DeepClone();
                }
                rect.Remove(legacy);
            }
        }
    }

    private static JsonObject? tryParseObject(string? json)
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
