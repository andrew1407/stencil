using System.Text.Json;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Application.Llm;

public static partial class OpPlanParser
{
    private static PlanAction normalize(JsonElement v, OpEntry entry) => entry.Name switch
    {
        "crop" => new CropAction(string.Join(' ',
            v.GetProperty("spec").EnumerateObject().Where(static p => p.Value.ValueKind != JsonValueKind.Null)
                .Select(static p => $"{p.Name}={p.Value.GetString()}"))),
        "rotate" => new RotateAction(str(v, entry, "dir")!, intOrDefault(v, entry, "times")),
        "filter" => new FilterAction(str(v, entry, "mode")!, str(v, entry, "tint")),
        "layout" => new LayoutAction(v.GetProperty("lines").EnumerateArray().Select(normalizeLine).ToList()),
        "formula" => optionalBool(v, "enabled") is bool enabled
            ? new FormulaAction(null, null, enabled)
            : new FormulaAction(str(v, entry, "axis"), str(v, entry, "expr")),
        "page" => str(v, entry, "format") is string format
            ? new PageAction(format)
            : new PageAction(null, optionalNumber(v, "width"), optionalNumber(v, "height")),
        "blank" => new BlankAction(str(v, entry, "color")!, str(v, entry, "format"), optionalNumber(v, "width"), optionalNumber(v, "height")),
        "frame" => new FrameAction(optionalInt(v, "index") is int index
            ? [index]
            : v.GetProperty("indices").EnumerateArray().Select(static x => readInt(x, "index")).ToList()),
        "image" => new ImageAction(optionalInt(v, "index")!.Value),
        "save" => new SaveAction(str(v, entry, "name"), str(v, entry, "path") is { Length: > 0 } path ? path : null),
        "undo" => new UndoAction(intOrDefault(v, entry, "steps")),
        "redo" => new RedoAction(intOrDefault(v, entry, "steps")),
        "reset" => new ResetAction(),
        "clear" => new ClearAction(),
        "clearChat" => new ClearChatAction(),
        "lineStyle" => new LineStyleAction(
            str(v, entry, "color"), str(v, entry, "pointColor"), optionalInt(v, "thickness"), optionalInt(v, "pointSize"),
            str(v, entry, "style"), str(v, entry, "drawMode"), str(v, entry, "fillColor")),
        // Whether the USER actually wrote the URL is the plan-level echo guard's job at execution.
        "openUrl" => new OpenUrlAction(str(v, entry, "url")!, optionalBool(v, "incognito") ?? false),
        "renameProject" => new RenameProjectAction(str(v, entry, "name")!),
        "describe" => new DescribeAction(str(v, entry, "text")!),
        "blankColor" => new BlankColorAction(str(v, entry, "color")!),
        "projectColor" => new ProjectColorAction(str(v, entry, "color")!),
        "export" => new ExportAction(str(v, entry, "what")!),
        // Which server a name means is resolved at EXECUTION time against the user's saved
        // connections.
        "connect" => new ConnectAction(str(v, entry, "server")!),
        "disconnect" => new DisconnectAction(str(v, entry, "server")!),
        _ => throw new InvalidOperationException($"registry op \"{entry.Name}\" has no normalizer"),
    };

    private static LayoutLine normalizeLine(JsonElement line) => new()
    {
        Points = line.GetProperty("points").EnumerateArray()
            .Select(static p => new LayoutPoint(p.GetProperty("x").GetDouble(), p.GetProperty("y").GetDouble())).ToList(),
        Color = optionalString(line, _emptyKeys, "color") ?? LayoutLine.DEFAULT_COLOR,
        Thickness = optionalNumber(line, "thickness") ?? LayoutLine.DEFAULT_THICKNESS,
        PointSize = optionalNumber(line, "pointSize") ?? LayoutLine.DEFAULT_POINT_SIZE,
        Style = optionalString(line, _emptyKeys, "style") ?? LayoutLine.DEFAULT_STYLE,
        Locked = optionalBool(line, "locked") ?? LayoutLine.DEFAULT_LOCKED,
        FillColor = optionalString(line, _emptyKeys, "fillColor") ?? LayoutLine.DEFAULT_FILL_COLOR,
    };

    private static readonly JsonElement _emptyKeys = JsonDocument.Parse("{}").RootElement.Clone();

    private static string? str(JsonElement v, OpEntry entry, string key) => optionalString(v, entry.Keys, key);

    private static string? optionalString(JsonElement obj, JsonElement keys, string key)
    {
        if (!hasField(obj, key))
        {
            return null;
        }
        string s = obj.GetProperty(key).GetString()!;
        return keys.TryGetProperty(key, out JsonElement spec) && spec.TryGetProperty("trim", out JsonElement t)
            && t.ValueKind == JsonValueKind.True
            ? s.Trim()
            : s;
    }

    private static bool? optionalBool(JsonElement obj, string key) =>
        hasField(obj, key) ? obj.GetProperty(key).GetBoolean() : null;

    private static double? optionalNumber(JsonElement obj, string key) =>
        hasField(obj, key) ? obj.GetProperty(key).GetDouble() : null;

    private static int? optionalInt(JsonElement obj, string key) =>
        hasField(obj, key) ? readInt(obj.GetProperty(key), key) : null;

    private static int intOrDefault(JsonElement obj, OpEntry entry, string key) =>
        optionalInt(obj, key) ?? readInt(entry.Spec(key)!.Value.GetProperty("default"), key);

    // The schema admits any finite integer; the typed actions hold 32-bit ones.
    private static int readInt(JsonElement v, string key)
    {
        double d = v.GetDouble();
        return d is >= int.MinValue and <= int.MaxValue
            ? (int)d
            : throw new PlanException($"\"{key}\" must fit a 32-bit integer");
    }
}
