using System.Text.Json;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Application.Llm;

// OpPlanParser — the per-op normalizers: each turns an action the registry schema has
// already validated (types, enums, ranges, grammars, presence rules) into its typed
// PlanAction, filling the registry defaults. Class doc lives in OpPlanParser.cs.
public static partial class OpPlanParser
{
    private static PlanAction Normalize(JsonElement v, OpEntry entry) => entry.Name switch
    {
        "crop" => new CropAction(string.Join(' ',
            v.GetProperty("spec").EnumerateObject().Where(static p => p.Value.ValueKind != JsonValueKind.Null)
                .Select(static p => $"{p.Name}={p.Value.GetString()}"))),
        "rotate" => new RotateAction(Str(v, entry, "dir")!, IntOrDefault(v, entry, "times")),
        "filter" => new FilterAction(Str(v, entry, "mode")!, Str(v, entry, "tint")),
        "layout" => new LayoutAction(v.GetProperty("lines").EnumerateArray().Select(NormalizeLine).ToList()),
        "formula" => OptionalBool(v, "enabled") is bool enabled
            ? new FormulaAction(null, null, enabled)
            : new FormulaAction(Str(v, entry, "axis"), Str(v, entry, "expr")),
        "page" => Str(v, entry, "format") is string format
            ? new PageAction(format)
            : new PageAction(null, OptionalNumber(v, "width"), OptionalNumber(v, "height")),
        "blank" => new BlankAction(Str(v, entry, "color")!, Str(v, entry, "format"), OptionalNumber(v, "width"), OptionalNumber(v, "height")),
        "frame" => new FrameAction(OptionalInt(v, "index") is int index
            ? [index]
            : v.GetProperty("indices").EnumerateArray().Select(static x => Int(x, "index")).ToList()),
        "image" => new ImageAction(OptionalInt(v, "index")!.Value),
        // A path that is empty after trimming is dropped (registry note: "" is dropped).
        "save" => new SaveAction(Str(v, entry, "name"), Str(v, entry, "path") is { Length: > 0 } path ? path : null),
        "undo" => new UndoAction(IntOrDefault(v, entry, "steps")),
        "redo" => new RedoAction(IntOrDefault(v, entry, "steps")),
        "reset" => new ResetAction(),
        "clear" => new ClearAction(),
        "clearChat" => new ClearChatAction(),
        "lineStyle" => new LineStyleAction(
            Str(v, entry, "color"), Str(v, entry, "pointColor"), OptionalInt(v, "thickness"), OptionalInt(v, "pointSize"),
            Str(v, entry, "style"), Str(v, entry, "drawMode"), Str(v, entry, "fillColor")),
        // Whether the USER actually wrote the URL is the plan-level echo guard's job at execution.
        "openUrl" => new OpenUrlAction(Str(v, entry, "url")!, OptionalBool(v, "incognito") ?? false),
        "renameProject" => new RenameProjectAction(Str(v, entry, "name")!),
        "describe" => new DescribeAction(Str(v, entry, "text")!),
        "blankColor" => new BlankColorAction(Str(v, entry, "color")!),
        "projectColor" => new ProjectColorAction(Str(v, entry, "color")!),
        "export" => new ExportAction(Str(v, entry, "what")!),
        // Which server a name means is resolved at EXECUTION time against the user's saved connections.
        "connect" => new ConnectAction(Str(v, entry, "server")!),
        "disconnect" => new DisconnectAction(Str(v, entry, "server")!),
        _ => throw new InvalidOperationException($"registry op \"{entry.Name}\" has no normalizer"),
    };

    private static LayoutLine NormalizeLine(JsonElement line) => new()
    {
        Points = line.GetProperty("points").EnumerateArray()
            .Select(static p => new LayoutPoint(p.GetProperty("x").GetDouble(), p.GetProperty("y").GetDouble())).ToList(),
        Color = OptionalString(line, EmptyKeys, "color") ?? LayoutLine.DefaultColor,
        Thickness = OptionalNumber(line, "thickness") ?? LayoutLine.DefaultThickness,
        PointSize = OptionalNumber(line, "pointSize") ?? LayoutLine.DefaultPointSize,
        Style = OptionalString(line, EmptyKeys, "style") ?? LayoutLine.DefaultStyle,
        Locked = OptionalBool(line, "locked") ?? LayoutLine.DefaultLocked,
        FillColor = OptionalString(line, EmptyKeys, "fillColor") ?? LayoutLine.DefaultFillColor,
    };

    private static readonly JsonElement EmptyKeys = JsonDocument.Parse("{}").RootElement.Clone();

    private static string? Str(JsonElement v, OpEntry entry, string key) => OptionalString(v, entry.Keys, key);

    /// <summary>An optional string: null when absent/null; trimmed when its spec says <c>trim</c>.</summary>
    private static string? OptionalString(JsonElement obj, JsonElement keys, string key)
    {
        if (!HasField(obj, key))
        {
            return null;
        }
        string s = obj.GetProperty(key).GetString()!;
        return keys.TryGetProperty(key, out JsonElement spec) && spec.TryGetProperty("trim", out JsonElement t)
            && t.ValueKind == JsonValueKind.True
            ? s.Trim()
            : s;
    }

    private static bool? OptionalBool(JsonElement obj, string key) =>
        HasField(obj, key) ? obj.GetProperty(key).GetBoolean() : null;

    private static double? OptionalNumber(JsonElement obj, string key) =>
        HasField(obj, key) ? obj.GetProperty(key).GetDouble() : null;

    private static int? OptionalInt(JsonElement obj, string key) =>
        HasField(obj, key) ? Int(obj.GetProperty(key), key) : null;

    /// <summary>The registry default when the key is absent (an integer key with a <c>default</c>).</summary>
    private static int IntOrDefault(JsonElement obj, OpEntry entry, string key) =>
        OptionalInt(obj, key) ?? Int(entry.Spec(key)!.Value.GetProperty("default"), key);

    // The schema admits any finite integer; the typed actions hold 32-bit ones.
    private static int Int(JsonElement v, string key)
    {
        double d = v.GetDouble();
        return d is >= int.MinValue and <= int.MaxValue
            ? (int)d
            : throw new PlanException($"\"{key}\" must fit a 32-bit integer");
    }
}
