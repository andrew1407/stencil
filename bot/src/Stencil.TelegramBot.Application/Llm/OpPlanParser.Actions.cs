using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Application.Llm;

// OpPlanParser — the per-op action parsers (crop … connect/disconnect) and their small field
// helpers, each enforcing its op's contract shape. Class doc lives in OpPlanParser.cs.
public static partial class OpPlanParser
{
    private static CropAction ParseCrop(JsonElement element)
    {
        RequireOnly(element, "crop", "spec", "aspect");
        // §1 tolerance: "aspect" beside "spec" is validated the same way and folded into
        // the spec when the spec lacks it; a conflicting duplicate fails the plan.
        string? beside = null;
        if (element.TryGetProperty("aspect", out JsonElement asideElement))
        {
            if (asideElement.ValueKind != JsonValueKind.String
                || asideElement.GetString() is not string asideToken
                || asideToken.Length > MaxStringField || !AspectRatio().IsMatch(asideToken))
            {
                throw new PlanException($"invalid \"crop\" action: bad token \"{Truncate(asideElement.ToString())}\" for aspect");
            }
            beside = asideToken;
        }
        if (!element.TryGetProperty("spec", out JsonElement spec) || spec.ValueKind != JsonValueKind.Object)
        {
            throw new PlanException("invalid \"crop\" action: \"spec\" must be an object");
        }
        List<string> parts = new();
        string? inside = null;
        foreach (JsonProperty property in spec.EnumerateObject())
        {
            if (Array.IndexOf(CropKeys, property.Name) < 0)
            {
                throw new PlanException($"invalid \"crop\" action: spec key \"{property.Name}\" is not one of x1/x2/y1/y2/aspect");
            }
            if (property.Value.ValueKind != JsonValueKind.String
                || property.Value.GetString() is not string token)
            {
                throw new PlanException($"invalid \"crop\" action: spec.{property.Name} must be a token string");
            }
            Regex shape = property.Name == "aspect" ? AspectRatio() : CropToken();
            if (token.Length > MaxStringField || !shape.IsMatch(token))
            {
                throw new PlanException($"invalid \"crop\" action: bad token \"{Truncate(token)}\" for {property.Name}");
            }
            inside = property.Name == "aspect" ? token : inside;
            parts.Add($"{property.Name}={token}");
        }
        if (beside is not null && inside is not null && beside != inside)
        {
            throw new PlanException("invalid \"crop\" action: conflicting \"aspect\" inside and beside \"spec\"");
        }
        if (beside is not null && inside is null)
        {
            parts.Add($"aspect={beside}");
        }
        if (parts.Count == 0)
        {
            throw new PlanException("invalid \"crop\" action: the spec needs at least one of x1/x2/y1/y2/aspect");
        }
        return new CropAction(string.Join(' ', parts));
    }

    private static RotateAction ParseRotate(JsonElement element)
    {
        RequireOnly(element, "rotate", "dir", "times");
        string dir = RequireString(element, "rotate", "dir");
        if (dir is not ("left" or "right"))
        {
            throw new PlanException("invalid \"rotate\" action: \"dir\" must be \"left\" or \"right\"");
        }
        int times = 1;
        if (element.TryGetProperty("times", out JsonElement timesElement))
        {
            if (timesElement.ValueKind != JsonValueKind.Number || !timesElement.TryGetInt32(out times)
                || times is < 1 or > 3)
            {
                throw new PlanException("invalid \"rotate\" action: \"times\" must be an integer 1..3");
            }
        }
        return new RotateAction(dir, times);
    }

    private static FilterAction ParseFilter(JsonElement element)
    {
        RequireOnly(element, "filter", "mode", "tint");
        string mode = RequireString(element, "filter", "mode");
        if (Array.IndexOf(FilterModes, mode) < 0)
        {
            throw new PlanException($"invalid \"filter\" action: unknown mode \"{Truncate(mode)}\"");
        }
        bool hasTint = element.TryGetProperty("tint", out JsonElement tintElement)
            && tintElement.ValueKind != JsonValueKind.Null;
        if (mode == "custom")
        {
            if (!hasTint || tintElement.ValueKind != JsonValueKind.String
                || tintElement.GetString() is not string tint || !HexColor().IsMatch(tint))
            {
                throw new PlanException("invalid \"filter\" action: mode \"custom\" requires \"tint\" as \"#rrggbb\"");
            }
            return new FilterAction(mode, tint);
        }
        if (hasTint)
        {
            throw new PlanException("invalid \"filter\" action: \"tint\" is only allowed with mode \"custom\"");
        }
        return new FilterAction(mode, null);
    }

    private static LayoutAction ParseLayout(JsonElement element)
    {
        RequireOnly(element, "layout", "lines");
        if (!element.TryGetProperty("lines", out JsonElement lines) || lines.ValueKind != JsonValueKind.Array)
        {
            throw new PlanException("invalid \"layout\" action: \"lines\" must be an array");
        }
        if (lines.GetArrayLength() > MaxLayoutLines)
        {
            throw new PlanException($"invalid \"layout\" action: too many lines (max {MaxLayoutLines})");
        }
        List<LayoutLine> parsed = new();
        foreach (JsonElement line in lines.EnumerateArray())
        {
            parsed.Add(ParseLine(line));
        }
        return new LayoutAction(parsed);
    }

    private static LayoutLine ParseLine(JsonElement line)
    {
        if (line.ValueKind != JsonValueKind.Object)
        {
            throw new PlanException("invalid \"layout\" action: each line must be a JSON object");
        }
        foreach (JsonProperty property in line.EnumerateObject())
        {
            if (Array.IndexOf(LineFields, property.Name) < 0)
            {
                throw new PlanException($"invalid \"layout\" action: unexpected line field \"{property.Name}\"");
            }
        }
        if (!line.TryGetProperty("points", out JsonElement points) || points.ValueKind != JsonValueKind.Array
            || points.GetArrayLength() == 0)
        {
            throw new PlanException("invalid \"layout\" action: each line needs a non-empty \"points\" array");
        }
        List<LayoutPoint> parsedPoints = new();
        foreach (JsonElement point in points.EnumerateArray())
        {
            if (point.ValueKind != JsonValueKind.Object
                || !point.TryGetProperty("x", out JsonElement x) || x.ValueKind != JsonValueKind.Number
                || !point.TryGetProperty("y", out JsonElement y) || y.ValueKind != JsonValueKind.Number)
            {
                throw new PlanException("invalid \"layout\" action: each point must be {\"x\":n,\"y\":n}");
            }
            parsedPoints.Add(new LayoutPoint(x.GetDouble(), y.GetDouble()));
        }
        string style = LineString(line, "style", LayoutLine.DefaultStyle);
        if (Array.IndexOf(LineStyles, style) < 0)
        {
            throw new PlanException("invalid \"layout\" action: line style must be solid/dashed/dotted");
        }
        return new LayoutLine
        {
            Points = parsedPoints,
            Color = LineString(line, "color", LayoutLine.DefaultColor),
            Thickness = LineNumber(line, "thickness", LayoutLine.DefaultThickness),
            PointSize = LineNumber(line, "pointSize", LayoutLine.DefaultPointSize),
            Style = style,
            Locked = LineBool(line, "locked", LayoutLine.DefaultLocked),
            FillColor = LineString(line, "fillColor", LayoutLine.DefaultFillColor),
        };
    }

    private static string LineString(JsonElement line, string name, string fallback)
    {
        if (!line.TryGetProperty(name, out JsonElement value))
        {
            return fallback;
        }
        if (value.ValueKind != JsonValueKind.String || value.GetString() is not string s
            || s.Length == 0 || s.Length > MaxStringField)
        {
            throw new PlanException($"invalid \"layout\" action: line \"{name}\" must be a non-empty string");
        }
        return s;
    }

    private static double LineNumber(JsonElement line, string name, double fallback)
    {
        if (!line.TryGetProperty(name, out JsonElement value))
        {
            return fallback;
        }
        if (value.ValueKind != JsonValueKind.Number)
        {
            throw new PlanException($"invalid \"layout\" action: line \"{name}\" must be a non-negative number");
        }
        double n = value.GetDouble();
        if (double.IsNaN(n) || double.IsInfinity(n) || n < 0)
        {
            throw new PlanException($"invalid \"layout\" action: line \"{name}\" must be a non-negative number");
        }
        return n;
    }

    private static bool LineBool(JsonElement line, string name, bool fallback)
    {
        if (!line.TryGetProperty(name, out JsonElement value))
        {
            return fallback;
        }
        if (value.ValueKind is not (JsonValueKind.True or JsonValueKind.False))
        {
            throw new PlanException($"invalid \"layout\" action: line \"{name}\" must be a boolean");
        }
        return value.GetBoolean();
    }

    private static FormulaAction ParseFormula(JsonElement element)
    {
        RequireOnly(element, "formula", "axis", "expr", "enabled");
        // §2: `enabled` rides ALONE — false switches formulas off entirely.
        if (element.TryGetProperty("enabled", out JsonElement enabledElement))
        {
            if (element.TryGetProperty("axis", out _) || element.TryGetProperty("expr", out _))
            {
                throw new PlanException("invalid \"formula\" action: \"enabled\" must ride alone (no axis/expr)");
            }
            if (enabledElement.ValueKind is not (JsonValueKind.True or JsonValueKind.False))
            {
                throw new PlanException("invalid \"formula\" action: \"enabled\" must be a boolean");
            }
            return new FormulaAction(null, null, enabledElement.GetBoolean());
        }
        string axis = RequireString(element, "formula", "axis");
        if (axis is not ("x" or "y"))
        {
            throw new PlanException("invalid \"formula\" action: \"axis\" must be \"x\" or \"y\"");
        }
        string expr = RequireString(element, "formula", "expr");
        if (expr.Length > MaxStringField)
        {
            throw new PlanException($"invalid \"formula\" action: \"expr\" is too long (max {MaxStringField} chars)");
        }
        // Charset [0-9xy+\-*/(). *] with the single variable matching the axis: the other
        // axis letter is as invalid as any foreign character.
        char variable = axis[0];
        foreach (char c in expr)
        {
            bool ok = char.IsAsciiDigit(c) || c == variable
                || c is '+' or '-' or '*' or '/' or '(' or ')' or '.' or ' ';
            if (!ok)
            {
                throw new PlanException($"invalid \"formula\" action: \"expr\" may only use digits, {variable}, + - * / ** ( ) . and spaces");
            }
        }
        return new FormulaAction(axis, expr);
    }

    private static PageAction ParsePage(JsonElement element)
    {
        RequireOnly(element, "page", "format", "width", "height");
        bool hasFormat = HasField(element, "format");
        bool hasDims = HasField(element, "width") || HasField(element, "height");
        // §2: an ISO format OR custom cm dims — exactly one of the two forms.
        if (hasFormat == hasDims)
        {
            throw new PlanException("invalid \"page\" action: give \"format\" OR \"width\"+\"height\" (exactly one form)");
        }
        if (hasFormat)
        {
            string format = RequireString(element, "page", "format");
            if (!PageFormat().IsMatch(format))
            {
                throw new PlanException("invalid \"page\" action: \"format\" must be a0…a10, b0…b10 or c0…c10");
            }
            return new PageAction(format);
        }
        if (!HasField(element, "width") || !HasField(element, "height"))
        {
            throw new PlanException("invalid \"page\" action: a custom size needs BOTH \"width\" and \"height\"");
        }
        return new PageAction(null, CmDim(element, "page", "width"), CmDim(element, "page", "height"));
    }

    private static BlankAction ParseBlank(JsonElement element)
    {
        RequireOnly(element, "blank", "color", "format", "width", "height");
        string color = RequireString(element, "blank", "color");
        if (!HexColor().IsMatch(color) && !CssColorName().IsMatch(color))
        {
            throw new PlanException("invalid \"blank\" action: \"color\" must be \"#rrggbb\" or a CSS colour name");
        }
        string? format = null;
        if (element.TryGetProperty("format", out JsonElement formatElement)
            && formatElement.ValueKind != JsonValueKind.Null)
        {
            if (formatElement.ValueKind != JsonValueKind.String
                || formatElement.GetString() is not string f || !PageFormat().IsMatch(f))
            {
                throw new PlanException("invalid \"blank\" action: \"format\" must be a0…a10, b0…b10 or c0…c10");
            }
            format = f;
        }
        // §2: explicit centimetre dims — both or neither; they override `format`.
        bool hasWidth = HasField(element, "width");
        bool hasHeight = HasField(element, "height");
        if (hasWidth != hasHeight)
        {
            throw new PlanException("invalid \"blank\" action: give BOTH \"width\" and \"height\" (in cm) or neither");
        }
        return hasWidth
            ? new BlankAction(color, format, CmDim(element, "blank", "width"), CmDim(element, "blank", "height"))
            : new BlankAction(color, format);
    }

    /// <summary>True when the field is present and non-null.</summary>
    private static bool HasField(JsonElement element, string name) =>
        element.TryGetProperty(name, out JsonElement value) && value.ValueKind != JsonValueKind.Null;

    /// <summary>A §2 custom page dimension: a number in centimetres, 0.1..500.</summary>
    private static double CmDim(JsonElement element, string op, string name)
    {
        if (!element.TryGetProperty(name, out JsonElement value) || value.ValueKind != JsonValueKind.Number)
        {
            throw new PlanException($"invalid \"{op}\" action: \"{name}\" must be a number in centimetres");
        }
        double cm = value.GetDouble();
        if (double.IsNaN(cm) || cm < MinPageCm || cm > MaxPageCm)
        {
            throw new PlanException($"invalid \"{op}\" action: \"{name}\" must be {MinPageCm}..{MaxPageCm} centimetres");
        }
        return cm;
    }

    private static FrameAction ParseFrame(JsonElement element)
    {
        RequireOnly(element, "frame", "index", "indices");
        bool hasIndex = element.TryGetProperty("index", out JsonElement indexElement);
        bool hasIndices = element.TryGetProperty("indices", out JsonElement indicesElement);
        if (hasIndex == hasIndices)
        {
            throw new PlanException("invalid \"frame\" action: give exactly one of \"index\" or \"indices\"");
        }
        if (hasIndex)
        {
            if (indexElement.ValueKind != JsonValueKind.Number || !indexElement.TryGetInt32(out int index) || index < 0)
            {
                throw new PlanException("invalid \"frame\" action: \"index\" must be an integer ≥ 0");
            }
            return new FrameAction([index]);
        }
        if (indicesElement.ValueKind != JsonValueKind.Array || indicesElement.GetArrayLength() == 0)
        {
            throw new PlanException("invalid \"frame\" action: \"indices\" must be a non-empty array");
        }
        if (indicesElement.GetArrayLength() > MaxFrameIndices)
        {
            throw new PlanException($"invalid \"frame\" action: too many indices (max {MaxFrameIndices})");
        }
        List<int> indices = new();
        foreach (JsonElement value in indicesElement.EnumerateArray())
        {
            if (value.ValueKind != JsonValueKind.Number || !value.TryGetInt32(out int index) || index < 0)
            {
                throw new PlanException("invalid \"frame\" action: every index must be an integer ≥ 0");
            }
            indices.Add(index);
        }
        return new FrameAction(indices);
    }

    /// <summary>§2.1 <c>image</c>: a 1-based index into the images attached to this turn.</summary>
    private static ImageAction ParseImage(JsonElement element)
    {
        RequireOnly(element, "image", "index");
        if (!element.TryGetProperty("index", out JsonElement indexElement)
            || indexElement.ValueKind != JsonValueKind.Number
            || !indexElement.TryGetInt32(out int index) || index < 1)
        {
            throw new PlanException("invalid \"image\" action: \"index\" must be an integer ≥ 1");
        }
        return new ImageAction(index);
    }

    /// <summary>
    /// §2.1 <c>save</c>: an optional project name (≤ 120 characters) and an optional §10
    /// destination path (≤ 1024 chars, trimmed, never a URL; empty after trim ≡ absent).
    /// The executor notes+skips the path — the bot saves to its usual place.
    /// </summary>
    private static SaveAction ParseSave(JsonElement element)
    {
        RequireOnly(element, "save", "name", "path");
        string? name = null;
        if (element.TryGetProperty("name", out JsonElement nameElement)
            && nameElement.ValueKind != JsonValueKind.Null)
        {
            if (nameElement.ValueKind != JsonValueKind.String
                || nameElement.GetString() is not string given || given.Length > MaxSaveName)
            {
                throw new PlanException($"invalid \"save\" action: \"name\" must be a string of at most {MaxSaveName} characters");
            }
            name = given;
        }
        string? path = null;
        if (element.TryGetProperty("path", out JsonElement pathElement)
            && pathElement.ValueKind != JsonValueKind.Null)
        {
            if (pathElement.ValueKind != JsonValueKind.String
                || pathElement.GetString() is not string raw || raw.Length > MaxPathChars)
            {
                throw new PlanException($"invalid \"save\" action: \"path\" must be a string of at most {MaxPathChars} characters");
            }
            string trimmed = raw.Trim();
            if (UrlScheme().IsMatch(trimmed))
            {
                throw new PlanException("invalid \"save\" action: \"path\" is a local path, not a URL");
            }
            path = trimmed.Length > 0 ? trimmed : null;
        }
        return new SaveAction(name, path);
    }

    /// <summary>§2 <c>undo</c>/<c>redo</c>: an optional <c>steps</c> int 1..20 (default 1).</summary>
    private static PlanAction ParseUndoRedo(JsonElement element, string op)
    {
        RequireOnly(element, op, "steps");
        int steps = 1;
        if (element.TryGetProperty("steps", out JsonElement stepsElement)
            && stepsElement.ValueKind != JsonValueKind.Null)
        {
            if (stepsElement.ValueKind != JsonValueKind.Number || !stepsElement.TryGetInt32(out steps)
                || steps < 1 || steps > MaxUndoSteps)
            {
                throw new PlanException($"invalid \"{op}\" action: \"steps\" must be an integer 1..{MaxUndoSteps}");
            }
        }
        return op == "undo" ? new UndoAction(steps) : new RedoAction(steps);
    }

    /// <summary>§2 <c>reset</c> / §10 <c>clear</c> + <c>clearChat</c>: no fields at all.</summary>
    private static T ParseFieldless<T>(JsonElement element, string op) where T : PlanAction, new()
    {
        RequireOnly(element, op);
        return new T();
    }

    /// <summary>
    /// §10 <c>lineStyle</c>: any subset of the pen-default fields (at least one), validated with
    /// the toolbar inputs' ranges. <c>pointColor</c>/<c>drawMode</c> parse per §10 but are noted
    /// and skipped at execution (the bot's pen has neither).
    /// </summary>
    private static LineStyleAction ParseLineStyle(JsonElement element)
    {
        RequireOnly(element, "lineStyle", "color", "pointColor", "thickness", "pointSize", "style", "drawMode", "fillColor");
        string? color = OptionalString(element, "lineStyle", "color");
        if (color is not null && !HexColor().IsMatch(color) && !CssColorName().IsMatch(color))
        {
            throw new PlanException("invalid \"lineStyle\" action: \"color\" must be \"#rrggbb\" or a CSS colour name");
        }
        string? pointColor = OptionalString(element, "lineStyle", "pointColor", allowEmpty: true);
        if (pointColor is not null && pointColor.Length > 0 && !HexColor().IsMatch(pointColor))
        {
            throw new PlanException("invalid \"lineStyle\" action: \"pointColor\" must be \"#rrggbb\" or \"\"");
        }
        int? thickness = OptionalInt(element, "lineStyle", "thickness", 1, 20);
        int? pointSize = OptionalInt(element, "lineStyle", "pointSize", 1, 30);
        string? style = OptionalString(element, "lineStyle", "style");
        if (style is not null && Array.IndexOf(LineStyles, style) < 0)
        {
            throw new PlanException("invalid \"lineStyle\" action: \"style\" must be solid/dashed/dotted");
        }
        string? drawMode = OptionalString(element, "lineStyle", "drawMode");
        if (drawMode is not null and not ("line" or "rect"))
        {
            throw new PlanException("invalid \"lineStyle\" action: \"drawMode\" must be \"line\" or \"rect\"");
        }
        string? fillColor = OptionalString(element, "lineStyle", "fillColor");
        if (fillColor is not null && fillColor != "transparent" && !HexColor().IsMatch(fillColor))
        {
            throw new PlanException("invalid \"lineStyle\" action: \"fillColor\" must be \"#rrggbb\" or \"transparent\"");
        }
        if (color is null && pointColor is null && thickness is null && pointSize is null
            && style is null && drawMode is null && fillColor is null)
        {
            throw new PlanException("invalid \"lineStyle\" action: give at least one field");
        }
        return new LineStyleAction(color, pointColor, thickness, pointSize, style, drawMode, fillColor);
    }

    /// <summary>
    /// §10 <c>openUrl</c>: an http(s) URL + optional <c>incognito</c> bool. Whether the USER
    /// actually wrote the URL is the plan-level echo guard's job at execution — this checks
    /// shape only.
    /// </summary>
    private static OpenUrlAction ParseOpenUrl(JsonElement element)
    {
        RequireOnly(element, "openUrl", "url", "incognito");
        bool incognito = false;
        if (element.TryGetProperty("incognito", out JsonElement incognitoElement)
            && incognitoElement.ValueKind != JsonValueKind.Null)
        {
            incognito = incognitoElement.ValueKind switch
            {
                JsonValueKind.True => true,
                JsonValueKind.False => false,
                _ => throw new PlanException("invalid \"openUrl\" action: \"incognito\" must be a boolean"),
            };
        }
        string url = RequireString(element, "openUrl", "url").Trim();
        if (url.Length == 0 || url.Length > MaxStringField || !IsHttpUrl(url))
        {
            throw new PlanException("invalid \"openUrl\" action: \"url\" must be an http(s) URL");
        }
        return new OpenUrlAction(url, incognito);
    }

    /// <summary>`http(s)://` + at least one character, none of them whitespace.</summary>
    private static bool IsHttpUrl(string url)
    {
        int scheme = url.StartsWith("https://", StringComparison.OrdinalIgnoreCase) ? 8
            : url.StartsWith("http://", StringComparison.OrdinalIgnoreCase) ? 7
            : 0;
        return scheme > 0 && url.Length > scheme && !url.Any(char.IsWhiteSpace);
    }

    /// <summary>§10 <c>renameProject</c>: a non-blank name, 1..80 chars.</summary>
    private static RenameProjectAction ParseRenameProject(JsonElement element)
    {
        RequireOnly(element, "renameProject", "name");
        string name = RequireString(element, "renameProject", "name").Trim();
        if (name.Length == 0 || name.Length > MaxRenameName)
        {
            throw new PlanException($"invalid \"renameProject\" action: \"name\" must be 1..{MaxRenameName} characters");
        }
        return new RenameProjectAction(name);
    }

    /// <summary>§10 <c>describe</c>: a description string ≤ 500 chars; <c>""</c> clears it.</summary>
    private static DescribeAction ParseDescribe(JsonElement element)
    {
        RequireOnly(element, "describe", "text");
        if (!element.TryGetProperty("text", out JsonElement textElement)
            || textElement.ValueKind != JsonValueKind.String
            || textElement.GetString() is not string text)
        {
            throw new PlanException("invalid \"describe\" action: \"text\" must be a string (\"\" clears)");
        }
        if (text.Length > MaxDescribeText)
        {
            throw new PlanException($"invalid \"describe\" action: \"text\" is longer than {MaxDescribeText} characters");
        }
        return new DescribeAction(text);
    }

    /// <summary>§10 <c>blankColor</c>: <c>#rrggbb</c> or a CSS colour name.</summary>
    private static BlankColorAction ParseBlankColor(JsonElement element)
    {
        RequireOnly(element, "blankColor", "color");
        string color = RequireString(element, "blankColor", "color");
        if (!HexColor().IsMatch(color) && !CssColorName().IsMatch(color))
        {
            throw new PlanException("invalid \"blankColor\" action: \"color\" must be \"#rrggbb\" or a CSS colour name");
        }
        return new BlankColorAction(color);
    }

    /// <summary>§10 <c>projectColor</c>: <c>#rrggbb</c>, or <c>""</c> to clear.</summary>
    private static ProjectColorAction ParseProjectColor(JsonElement element)
    {
        RequireOnly(element, "projectColor", "color");
        string color = RequireString(element, "projectColor", "color");
        if (color.Length > 0 && !HexColor().IsMatch(color))
        {
            throw new PlanException("invalid \"projectColor\" action: \"color\" must be \"#rrggbb\" or \"\"");
        }
        return new ProjectColorAction(color);
    }

    /// <summary>§10 <c>export</c>: <c>what</c> is <c>layout</c> or <c>project</c>.</summary>
    private static ExportAction ParseExport(JsonElement element)
    {
        RequireOnly(element, "export", "what");
        string what = RequireString(element, "export", "what");
        if (what is not ("layout" or "project"))
        {
            throw new PlanException("invalid \"export\" action: \"what\" must be \"layout\" or \"project\"");
        }
        return new ExportAction(what);
    }

    /// <summary>An optional string field: null when absent/null; non-string (or empty unless allowed) fails.</summary>
    private static string? OptionalString(JsonElement element, string op, string name, bool allowEmpty = false)
    {
        if (!element.TryGetProperty(name, out JsonElement value) || value.ValueKind == JsonValueKind.Null)
        {
            return null;
        }
        if (value.ValueKind != JsonValueKind.String || value.GetString() is not string s
            || (!allowEmpty && s.Length == 0) || s.Length > MaxStringField)
        {
            throw new PlanException($"invalid \"{op}\" action: \"{name}\" must be a non-empty string");
        }
        return s;
    }

    /// <summary>An optional bounded int field: null when absent/null; out-of-range fails.</summary>
    private static int? OptionalInt(JsonElement element, string op, string name, int min, int max)
    {
        if (!element.TryGetProperty(name, out JsonElement value) || value.ValueKind == JsonValueKind.Null)
        {
            return null;
        }
        if (value.ValueKind != JsonValueKind.Number || !value.TryGetInt32(out int n) || n < min || n > max)
        {
            throw new PlanException($"invalid \"{op}\" action: \"{name}\" must be an integer {min}..{max}");
        }
        return n;
    }

    /// <summary>
    /// §10 <c>connect</c>/<c>disconnect</c>: a non-empty <c>server</c> string and nothing else —
    /// plans never carry tokens (<see cref="RequireOnly"/> rejects a <c>token</c> field). Which
    /// server it names is resolved at EXECUTION time against the user's saved connections.
    /// </summary>
    private static PlanAction ParseServerOp(JsonElement element, string op)
    {
        RequireOnly(element, op, "server");
        string server = RequireString(element, op, "server");
        if (server.Trim().Length == 0 || server.Length > MaxStringField)
        {
            throw new PlanException($"invalid \"{op}\" action: \"server\" must be a non-empty string");
        }
        return op == "connect" ? new ConnectAction(server) : new DisconnectAction(server);
    }

    /// <summary>A label in parentheses for a drop warning, or nothing when it carried none.</summary>
    private static string Named(string label) =>
        label.Trim() is { Length: > 0 } name ? $" (\"{Truncate(name)}\")" : "";

    /// <summary>Clip a value echoed into an error message so a huge field can't flood the chat.</summary>
    private static string Truncate(string value) =>
        value.Length <= MaxEchoedChars ? value : value[..MaxEchoedChars] + "…";
}
