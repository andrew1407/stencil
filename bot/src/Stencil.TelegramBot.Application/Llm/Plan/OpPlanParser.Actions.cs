using System.Text.Json;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Application.Llm.Schema;

namespace Stencil.TelegramBot.Application.Llm.Plan;

public static partial class OpPlanParser
{
    private static PlanAction normalize(JsonElement v, OpEntry entry) =>
        _normalizers.TryGetValue(entry.Name, out Func<JsonElement, OpEntry, PlanAction>? make)
            ? make(v, entry)
            : throw new InvalidOperationException($"registry op \"{entry.Name}\" has no normalizer");

    // One factory per registry op, keyed by its name.
    private static readonly Dictionary<string, Func<JsonElement, OpEntry, PlanAction>> _normalizers = new(StringComparer.Ordinal)
    {
        ["crop"] = static (v, entry) => new CropAction(string.Join(' ',
            v.GetProperty("spec").EnumerateObject().Where(static p => p.Value.ValueKind != JsonValueKind.Null)
                .Select(static p => $"{p.Name}={p.Value.GetString()}"))),
        ["rotate"] = static (v, entry) => new RotateAction(str(v, entry, "dir")!, intOrDefault(v, entry, "times")),
        ["filter"] = static (v, entry) => new FilterAction(str(v, entry, "mode")!, str(v, entry, "tint")),
        ["layout"] = static (v, entry) => new LayoutAction(v.GetProperty("lines").EnumerateArray().Select(normalizeLine).ToList()),
        ["formula"] = static (v, entry) => optionalBool(v, "enabled") is bool enabled
            ? new FormulaAction(null, null, enabled)
            : new FormulaAction(str(v, entry, "axis"), str(v, entry, "expr")),
        ["page"] = static (v, entry) => str(v, entry, "format") is string format
            ? new PageAction(format)
            : new PageAction(null, optionalNumber(v, "width"), optionalNumber(v, "height")),
        ["blank"] = static (v, entry) => new BlankAction(str(v, entry, "color")!, str(v, entry, "format"), optionalNumber(v, "width"), optionalNumber(v, "height")),
        ["frame"] = static (v, entry) => new FrameAction(optionalInt(v, "index") is int index
            ? [index]
            : v.GetProperty("indices").EnumerateArray().Select(static x => readInt(x, "index")).ToList()),
        ["image"] = static (v, entry) => new ImageAction(optionalInt(v, "index")!.Value),
        ["save"] = static (v, entry) => new SaveAction(str(v, entry, "name"), str(v, entry, "path") is { Length: > 0 } path ? path : null),
        ["undo"] = static (v, entry) => new UndoAction(intOrDefault(v, entry, "steps")),
        ["redo"] = static (v, entry) => new RedoAction(intOrDefault(v, entry, "steps")),
        ["reset"] = static (v, entry) => new ResetAction(),
        ["clear"] = static (v, entry) => new ClearAction(),
        ["clearChat"] = static (v, entry) => new ClearChatAction(),
        ["lineStyle"] = static (v, entry) => new LineStyleAction(
            str(v, entry, "color"), str(v, entry, "pointColor"), optionalInt(v, "thickness"), optionalInt(v, "pointSize"),
            str(v, entry, "style"), str(v, entry, "drawMode"), str(v, entry, "fillColor")),
        // Whether the USER actually wrote the URL is the plan-level echo guard's job at execution.
        ["openUrl"] = static (v, entry) => new OpenUrlAction(str(v, entry, "url")!, optionalBool(v, "incognito") ?? false),
        ["renameProject"] = static (v, entry) => new RenameProjectAction(str(v, entry, "name")!),
        ["describe"] = static (v, entry) => new DescribeAction(str(v, entry, "text")!),
        ["blankColor"] = static (v, entry) => new BlankColorAction(str(v, entry, "color")!),
        ["projectColor"] = static (v, entry) => new ProjectColorAction(str(v, entry, "color")!),
        ["export"] = static (v, entry) => new ExportAction(str(v, entry, "what")!),
        // Which server a name means is resolved at EXECUTION time against the user's saved
        // connections.
        ["connect"] = static (v, entry) => new ConnectAction(str(v, entry, "server")!),
        ["disconnect"] = static (v, entry) => new DisconnectAction(str(v, entry, "server")!),
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
