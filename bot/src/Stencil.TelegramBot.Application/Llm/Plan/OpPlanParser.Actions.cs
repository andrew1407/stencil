using System.Text.Json;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Application.Llm.Plan;

// Core's normalized actions — declared keys only, defaults applied, trims honoured — onto the
// typed union; nothing here validates beyond what the typed actions can hold.
public static partial class OpPlanParser
{
    private static readonly Dictionary<string, Func<JsonElement, PlanAction>> _mappers = new(StringComparer.Ordinal)
    {
        ["crop"] = static v => new CropAction(string.Join(' ', v.GetProperty("spec").EnumerateObject()
            .Where(static p => p.Value.ValueKind == JsonValueKind.String).Select(static p => $"{p.Name}={p.Value.GetString()}"))),
        ["rotate"] = static v => new RotateAction(must(v, "dir"), mustInt(v, "times")),
        ["filter"] = static v => new FilterAction(must(v, "mode"), str(v, "tint")),
        ["layout"] = static v => new LayoutAction([.. v.GetProperty("lines").EnumerateArray().Select(mapLine)]),
        ["formula"] = static v => boolean(v, "enabled") is bool enabled
            ? new FormulaAction(null, null, enabled)
            : new FormulaAction(str(v, "axis"), str(v, "expr")),
        ["page"] = static v => str(v, "format") is string format
            ? new PageAction(format)
            : new PageAction(null, number(v, "width"), number(v, "height")),
        ["blank"] = static v => new BlankAction(must(v, "color"), str(v, "format"), number(v, "width"), number(v, "height")),
        ["frame"] = static v => new FrameAction(optionalInt(v, "index") is int index
            ? [index]
            : [.. v.GetProperty("indices").EnumerateArray().Select(static x => readInt(x, "index"))]),
        ["image"] = static v => new ImageAction(mustInt(v, "index")),
        ["save"] = static v => new SaveAction(str(v, "name"), str(v, "path") is { Length: > 0 } path ? path : null),
        ["undo"] = static v => new UndoAction(mustInt(v, "steps")),
        ["redo"] = static v => new RedoAction(mustInt(v, "steps")),
        ["reset"] = static _ => new ResetAction(),
        ["clear"] = static _ => new ClearAction(),
        ["clearChat"] = static _ => new ClearChatAction(),
        ["lineStyle"] = static v => new LineStyleAction(
            str(v, "color"), str(v, "pointColor"), optionalInt(v, "thickness"), optionalInt(v, "pointSize"),
            str(v, "style"), str(v, "drawMode"), str(v, "fillColor")),
        // Whether the USER actually wrote the URL is the plan-level echo guard's job at execution.
        ["openUrl"] = static v => new OpenUrlAction(must(v, "url"), boolean(v, "incognito") ?? false),
        ["renameProject"] = static v => new RenameProjectAction(must(v, "name")),
        ["describe"] = static v => new DescribeAction(must(v, "text")),
        ["blankColor"] = static v => new BlankColorAction(must(v, "color")),
        ["projectColor"] = static v => new ProjectColorAction(must(v, "color")),
        ["export"] = static v => new ExportAction(must(v, "what")),
        // Which server a name means is resolved at EXECUTION time against the user's saved
        // connections.
        ["connect"] = static v => new ConnectAction(must(v, "server")),
        ["disconnect"] = static v => new DisconnectAction(must(v, "server")),
    };

    private static LayoutLine mapLine(JsonElement line) => new()
    {
        Points = [.. line.GetProperty("points").EnumerateArray()
            .Select(static p => new LayoutPoint(p.GetProperty("x").GetDouble(), p.GetProperty("y").GetDouble()))],
        Color = str(line, "color") ?? LayoutLine.DEFAULT_COLOR,
        Thickness = number(line, "thickness") ?? LayoutLine.DEFAULT_THICKNESS,
        PointSize = number(line, "pointSize") ?? LayoutLine.DEFAULT_POINT_SIZE,
        Style = str(line, "style") ?? LayoutLine.DEFAULT_STYLE,
        Locked = boolean(line, "locked") ?? LayoutLine.DEFAULT_LOCKED,
        FillColor = str(line, "fillColor") ?? LayoutLine.DEFAULT_FILL_COLOR,
        PointColor = str(line, "pointColor") ?? LayoutLine.DEFAULT_POINT_COLOR,
    };

    private static string must(JsonElement v, string key) =>
        str(v, key) ?? throw new PlanException($"\"{key}\" is missing");

    private static int mustInt(JsonElement v, string key) =>
        optionalInt(v, key) ?? throw new PlanException($"\"{key}\" is missing");

    // Core's own text for a failure or a warning; a result without one breaks cli/CONTRACT.md §7.
    private static string message(JsonElement o) => o.GetProperty("message").GetString() ?? "";

    private static string? str(JsonElement o, string key) =>
        o.TryGetProperty(key, out JsonElement v) && v.ValueKind == JsonValueKind.String ? v.GetString() : null;

    private static int? integer(JsonElement o, string key) =>
        o.TryGetProperty(key, out JsonElement v) && v.ValueKind == JsonValueKind.Number && v.TryGetInt32(out int n) ? n : null;

    private static bool? boolean(JsonElement o, string key) =>
        o.TryGetProperty(key, out JsonElement v) && v.ValueKind is JsonValueKind.True or JsonValueKind.False ? v.GetBoolean() : null;

    private static double? number(JsonElement o, string key) =>
        o.TryGetProperty(key, out JsonElement v) && v.ValueKind == JsonValueKind.Number ? v.GetDouble() : null;

    private static int? optionalInt(JsonElement o, string key) =>
        o.TryGetProperty(key, out JsonElement v) && v.ValueKind == JsonValueKind.Number ? readInt(v, key) : null;

    // Core admits any finite integer; the typed actions hold 32-bit ones.
    private static int readInt(JsonElement v, string key)
    {
        double d = v.GetDouble();
        return d is >= int.MinValue and <= int.MaxValue
            ? (int)d
            : throw new PlanException($"\"{key}\" must fit a 32-bit integer");
    }
}
