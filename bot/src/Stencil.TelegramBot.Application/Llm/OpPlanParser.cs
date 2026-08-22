using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Application.Llm;

/// <summary>
/// The outcome of parsing an LLM reply: a plan (always present on success — a reply with no
/// JSON object at all is a valid <i>chat-only</i> plan), warnings for skipped unknown ops, or
/// a plan-level <see cref="Error"/> (nothing executes; the error is shown in chat).
/// </summary>
public sealed record OpPlanParseResult(OpPlan? Plan, IReadOnlyList<string> Warnings, string? Error);

/// <summary>
/// Op-plan extraction + strict validation, implementing <c>llm-contract.md</c> §1–3 exactly:
/// fences stripped, the first balanced <c>{…}</c> JSON object taken (none ⇒ chat-only); an
/// unknown <c>op</c> drops with a warning, a known op with invalid params fails the WHOLE plan
/// — except §1's leniency: a variant or ask-preview carrying a top-level-only/settings op is
/// dropped alone. The per-op action parsers live in OpPlanParser.Actions.cs.
/// </summary>
public static partial class OpPlanParser
{
    public const int MaxActions = 16;
    public const int MaxVariants = 8;
    public const int MaxLayoutLines = 200;
    public const int MaxStringField = 5000;
    public const int MaxFrameIndices = 32;

    /// <summary>The §2.1 <c>save</c> name cap — the same 120 characters in every client.</summary>
    public const int MaxSaveName = 120;

    /// <summary>§10: the longest local path a <c>save</c> op may carry — the same 1024 in every client.</summary>
    public const int MaxPathChars = 1024;

    /// <summary>§2 <c>undo</c>/<c>redo</c>: the <c>steps</c> bound (1..20, default 1).</summary>
    public const int MaxUndoSteps = 20;

    /// <summary>§10 <c>describe</c>: the description cap (500 chars; empty clears).</summary>
    public const int MaxDescribeText = 500;

    /// <summary>§10 <c>renameProject</c>: the name cap (1..80 chars).</summary>
    public const int MaxRenameName = 80;

    // §2 page/blank custom dims: centimetres, 0.1..500.
    public const double MinPageCm = 0.1;
    public const double MaxPageCm = 500;

    // §11 interactive replies — the same numbers as every other client.
    public const int MinAskOptions = 2;
    public const int MaxAskOptions = 5;
    public const int MaxAskQuestion = 300;
    public const int MaxAskLabel = 80;
    public const int MaxAskAnswer = 500;
    public const string DefaultCustomLabel = "Something else…";

    /// <summary>Longest value echoed into an error message (so a huge field can't flood the chat).</summary>
    private const int MaxEchoedChars = 40;

    private static readonly string[] FilterModes = ["none", "bw", "sepia", "invert", "contour", "custom"];
    private static readonly string[] LineStyles = ["solid", "dashed", "dotted"];
    private static readonly string[] CropKeys = ["x1", "x2", "y1", "y2", "aspect"];
    private static readonly string[] LineFields = ["points", "color", "thickness", "pointSize", "style", "locked", "fillColor"];

    /// <summary>§2/§2.1 top-level-only ops — banned inside variants AND ask previews (§13: registry-derived).</summary>
    private static readonly string[] TopLevelOnlyOps = OpRegistry.TopLevelOnlyNames;

    /// <summary>
    /// The §10-scoped ops the bot carries (§13: registry-derived). Not image edits: banned
    /// inside variants (which exist to produce images) and inside ask-option previews, per
    /// the existing §10 rule.
    /// </summary>
    private static readonly string[] SettingsOps = OpRegistry.SettingsNames;

    /// <summary>
    /// Every op name <see cref="ParseAction"/>'s dispatch handles — the mirror of its switch
    /// cases. Tests cross-check this set against <see cref="OpRegistry.Names"/> and against the
    /// dispatch itself (each name must parse as a KNOWN op, never the §1 unknown-op skip).
    /// </summary>
    public static readonly IReadOnlyList<string> KnownOps =
    [
        "crop", "rotate", "filter", "layout", "formula", "page", "blank", "frame",
        "image", "save", "undo", "redo", "reset", "clear", "lineStyle", "openUrl",
        "renameProject", "describe", "blankColor", "projectColor", "export",
        "connect", "disconnect", "clearChat",
    ];

    [GeneratedRegex(@"^-?(\d+(\.\d+)?|\.\d+)(%|px|cm|in)?$")]
    private static partial Regex CropToken();

    // "W:H" with strictly positive integers (leading zeros tolerated, like the core parser).
    [GeneratedRegex("^0*[1-9][0-9]*:0*[1-9][0-9]*$")]
    private static partial Regex AspectRatio();

    [GeneratedRegex("^#[0-9a-fA-F]{6}$")]
    private static partial Regex HexColor();

    [GeneratedRegex("^[a-zA-Z]+$")]
    private static partial Regex CssColorName();

    [GeneratedRegex("^[abc]([0-9]|10)$")]
    private static partial Regex PageFormat();

    // The shared save-path shape check: a scheme:// prefix marks a URL, not a local path.
    [GeneratedRegex("^[A-Za-z][A-Za-z0-9+.-]*://")]
    private static partial Regex UrlScheme();

    /// <summary>Validation failure for a known op — fails the whole plan (contract §1).</summary>
    private sealed class PlanException : Exception
    {
        public PlanException(string message) : base(message) { }
    }

    /// <summary>
    /// A top-level-only or settings op found inside a variant / ask-option preview — §1's one
    /// leniency: that variant (or that option's preview) is dropped with a warning, never the
    /// whole plan. The message is the "why" fragment the warning quotes.
    /// </summary>
    private sealed class MisplacedOpException : Exception
    {
        public MisplacedOpException(string message) : base(message) { }
    }

    /// <summary>Parse one raw LLM reply into a plan, warnings, or a plan-level error.</summary>
    public static OpPlanParseResult Parse(string? raw)
    {
        string text = (raw ?? "").Trim();
        if (!TryExtractJsonObject(StripFences(text), out JsonDocument? doc))
        {
            // No JSON object at all: the turn is chat-only — the raw text is the reply.
            return new OpPlanParseResult(new OpPlan(text, [], []), [], null);
        }
        using (doc)
        {
            List<string> warnings = new();
            try
            {
                OpPlan plan = ParsePlan(doc!.RootElement, warnings);
                return new OpPlanParseResult(plan, warnings, null);
            }
            catch (PlanException ex)
            {
                return new OpPlanParseResult(null, warnings, ex.Message);
            }
        }
    }

    /// <summary>Drop Markdown code-fence lines (```/```json) before object extraction.</summary>
    private static string StripFences(string text)
    {
        if (!text.Contains("```"))
        {
            return text;
        }
        StringBuilder sb = new(text.Length);
        foreach (string line in text.Split('\n'))
        {
            if (line.TrimStart().StartsWith("```", StringComparison.Ordinal))
            {
                continue;
            }
            sb.Append(line).Append('\n');
        }
        return sb.ToString();
    }

    /// <summary>
    /// Find the first balanced <c>{…}</c> substring that parses as JSON (string/escape-aware
    /// brace matching, retrying from each later <c>{</c> so stray braces in prose don't hide a
    /// real object).
    /// </summary>
    private static bool TryExtractJsonObject(string text, out JsonDocument? doc)
    {
        for (int start = text.IndexOf('{'); start >= 0; start = text.IndexOf('{', start + 1))
        {
            if (!TryFindBalancedEnd(text, start, out int end))
            {
                continue;
            }
            try
            {
                doc = JsonDocument.Parse(text[start..(end + 1)]);
                return true;
            }
            catch (JsonException)
            {
                // Balanced but not JSON (e.g. braces in prose) — try the next candidate.
            }
        }
        doc = null;
        return false;
    }

    /// <summary>Index of the matching <c>}</c> for the <c>{</c> at <paramref name="start"/>.</summary>
    private static bool TryFindBalancedEnd(string text, int start, out int end)
    {
        int depth = 0;
        bool inString = false;
        for (int i = start; i < text.Length; i++)
        {
            char c = text[i];
            if (inString)
            {
                if (c == '\\')
                {
                    i++; // skip the escaped character
                }
                else if (c == '"')
                {
                    inString = false;
                }
                continue;
            }
            switch (c)
            {
                case '"': inString = true; break;
                case '{': depth++; break;
                case '}':
                    depth--;
                    if (depth == 0)
                    {
                        end = i;
                        return true;
                    }
                    break;
            }
        }
        end = -1;
        return false;
    }

    private static OpPlan ParsePlan(JsonElement root, List<string> warnings)
    {
        if (root.ValueKind != JsonValueKind.Object)
        {
            throw new PlanException("the plan is not a JSON object");
        }
        // `version` other than 1 (or absent) is accepted but ignored (contract §1).
        // §1 reply tolerance: models routinely omit the reply while planning valid
        // actions — substitute rather than lose the plan to a missing pleasantry.
        // The substitute itself is chosen below, once the plan's contents are known.
        bool replyOmitted = !root.TryGetProperty("reply", out JsonElement replyElement)
            || replyElement.ValueKind != JsonValueKind.String
            || replyElement.GetString() is not string replyText
            || replyText.Trim().Length == 0;
        string reply = replyOmitted ? string.Empty : replyElement.GetString()!;
        IReadOnlyList<PlanAction> actions = ParseActionList(root, "actions", warnings);
        List<OpVariant> variants = new();
        if (root.TryGetProperty("variants", out JsonElement variantsElement)
            && variantsElement.ValueKind != JsonValueKind.Null)
        {
            if (variantsElement.ValueKind != JsonValueKind.Array)
            {
                throw new PlanException("\"variants\" must be an array");
            }
            if (variantsElement.GetArrayLength() > MaxVariants)
            {
                throw new PlanException($"too many variants (max {MaxVariants})");
            }
            int number = 0;
            foreach (JsonElement variantElement in variantsElement.EnumerateArray())
            {
                number++;
                if (variantElement.ValueKind != JsonValueKind.Object)
                {
                    throw new PlanException("each variant must be a JSON object");
                }
                string label = variantElement.TryGetProperty("label", out JsonElement labelElement)
                    && labelElement.ValueKind == JsonValueKind.String
                    ? labelElement.GetString() ?? ""
                    : "";
                // §1's one leniency: a misplaced op costs THIS variant its place, not the turn.
                // Its own warnings go with it — nothing of it will run.
                List<string> variantWarnings = new();
                try
                {
                    IReadOnlyList<PlanAction> variantActions =
                        ParseActionList(variantElement, "actions", variantWarnings, inVariant: true);
                    warnings.AddRange(variantWarnings);
                    variants.Add(new OpVariant(label, variantActions));
                }
                catch (MisplacedOpException ex)
                {
                    warnings.Add($"Dropped variant {number}{Named(label)} — {ex.Message} and can't ride inside a variant.");
                }
            }
        }
        AskCard? ask = ParseAsk(root, warnings);
        // "Done." only when the plan actually carries work — a bare "Done." on
        // an empty plan reads as a success that never occurred (contract §1).
        if (replyOmitted)
        {
            if (actions.Count > 0 || variants.Count > 0 || ask is not null)
            {
                reply = "Done.";
                warnings.Add("The model omitted its reply — the plan still ran");
            }
            else
            {
                reply = "The model returned an empty plan — nothing was changed.";
            }
        }
        return new OpPlan(reply, actions, variants, ask);
    }

    /// <summary>
    /// Parse the optional <c>ask</c> object (contract §11) — the question, its 2..5 options, and
    /// the free-text row. Validated as strictly as an action: a card nobody can answer rejects
    /// the whole plan rather than reaching the user as a broken prompt.
    /// </summary>
    private static AskCard? ParseAsk(JsonElement root, List<string> warnings)
    {
        if (!root.TryGetProperty("ask", out JsonElement ask) || ask.ValueKind == JsonValueKind.Null)
        {
            return null;
        }
        if (ask.ValueKind != JsonValueKind.Object)
        {
            throw new PlanException("\"ask\" must be a JSON object");
        }
        foreach (JsonProperty property in ask.EnumerateObject())
        {
            if (property.Name is not ("question" or "mode" or "options" or "allowCustom" or "customLabel"))
            {
                throw new PlanException($"\"ask\" has an unknown field \"{property.Name}\"");
            }
        }
        if (!ask.TryGetProperty("question", out JsonElement questionElement)
            || questionElement.ValueKind != JsonValueKind.String
            || questionElement.GetString()?.Trim() is not string question
            || question.Length == 0)
        {
            throw new PlanException("\"ask.question\" must be a non-empty string");
        }
        if (question.Length > MaxAskQuestion)
        {
            throw new PlanException($"\"ask.question\" is longer than {MaxAskQuestion} characters");
        }

        bool multi = false;
        if (ask.TryGetProperty("mode", out JsonElement modeElement) && modeElement.ValueKind != JsonValueKind.Null)
        {
            string? mode = modeElement.ValueKind == JsonValueKind.String ? modeElement.GetString() : null;
            multi = mode switch
            {
                "multi" => true,
                "single" => false,
                _ => throw new PlanException("\"ask.mode\" must be \"single\" or \"multi\""),
            };
        }

        bool allowCustom = false;
        if (ask.TryGetProperty("allowCustom", out JsonElement customElement) && customElement.ValueKind != JsonValueKind.Null)
        {
            allowCustom = customElement.ValueKind switch
            {
                JsonValueKind.True => true,
                JsonValueKind.False => false,
                _ => throw new PlanException("\"ask.allowCustom\" must be a boolean"),
            };
        }

        string customLabel = DefaultCustomLabel;
        if (ask.TryGetProperty("customLabel", out JsonElement labelElement) && labelElement.ValueKind != JsonValueKind.Null)
        {
            if (labelElement.ValueKind != JsonValueKind.String
                || labelElement.GetString()?.Trim() is not string custom
                || custom.Length == 0)
            {
                throw new PlanException("\"ask.customLabel\" must be a non-empty string");
            }
            if (custom.Length > MaxAskLabel)
            {
                throw new PlanException($"\"ask.customLabel\" is longer than {MaxAskLabel} characters");
            }
            customLabel = custom;
        }

        if (!ask.TryGetProperty("options", out JsonElement optionsElement)
            || optionsElement.ValueKind != JsonValueKind.Array)
        {
            throw new PlanException("\"ask.options\" must be an array");
        }
        int count = optionsElement.GetArrayLength();
        if (count < MinAskOptions || count > MaxAskOptions)
        {
            throw new PlanException($"\"ask.options\" must hold {MinAskOptions}..{MaxAskOptions} options");
        }

        List<AskOption> options = new(count);
        bool droppedPreview = false;
        int index = 0;
        foreach (JsonElement optionElement in optionsElement.EnumerateArray())
        {
            index++;
            if (optionElement.ValueKind != JsonValueKind.Object)
            {
                throw new PlanException($"ask option {index} must be a JSON object");
            }
            foreach (JsonProperty property in optionElement.EnumerateObject())
            {
                if (property.Name is not ("label" or "actions" or "image"))
                {
                    throw new PlanException($"ask option {index} has an unknown field \"{property.Name}\"");
                }
            }
            if (!optionElement.TryGetProperty("label", out JsonElement optionLabel)
                || optionLabel.ValueKind != JsonValueKind.String
                || optionLabel.GetString()?.Trim() is not string label
                || label.Length == 0)
            {
                throw new PlanException($"ask option {index} \"label\" must be a non-empty string");
            }
            if (label.Length > MaxAskLabel)
            {
                throw new PlanException($"ask option {index} \"label\" is longer than {MaxAskLabel} characters");
            }
            bool hasActions = optionElement.TryGetProperty("actions", out JsonElement actionsElement)
                && actionsElement.ValueKind != JsonValueKind.Null;
            bool hasImage = optionElement.TryGetProperty("image", out JsonElement imageElement)
                && imageElement.ValueKind != JsonValueKind.Null;
            if (hasActions && hasImage)
            {
                throw new PlanException($"ask option {index} carries both \"actions\" and \"image\"");
            }
            if (hasImage)
            {
                ValidateAskImage(imageElement, index, warnings);
            }
            if (hasActions)
            {
                // §1: a preview asking for a top-level-only or settings op loses the PREVIEW
                // and says which option and why — the option, and the plan, stand.
                if (MisplacedPreviewOp(actionsElement) is string why)
                {
                    warnings.Add($"Dropped the preview for option {index}{Named(label)} — {why} and can't ride inside an ask option's preview.");
                }
                else
                {
                    // No working image in a chat: the option stands, its rendered preview doesn't.
                    droppedPreview = true;
                }
            }
            options.Add(new AskOption(label));
        }
        if (droppedPreview)
        {
            warnings.Add("some options asked for a rendered preview, which this chat can't produce — they are listed by name");
        }
        return new AskCard(question, multi, allowCustom, customLabel, options);
    }

    /// <summary>
    /// The first top-level-only or §10 settings op inside an ask option's preview <c>actions</c>
    /// — the "why" fragment for §1's drop warning, or null when the preview is well-formed (the
    /// rest is dropped unread either way: this chat renders no previews).
    /// </summary>
    private static string? MisplacedPreviewOp(JsonElement actions)
    {
        if (actions.ValueKind != JsonValueKind.Array)
        {
            return null;
        }
        foreach (JsonElement action in actions.EnumerateArray())
        {
            if (action.ValueKind != JsonValueKind.Object
                || !action.TryGetProperty("op", out JsonElement op)
                || op.ValueKind != JsonValueKind.String
                || op.GetString() is not string name)
            {
                continue;
            }
            if (Array.IndexOf(TopLevelOnlyOps, name) >= 0)
            {
                return $"\"{name}\" is a top-level action only (§2.1)";
            }
            if (Array.IndexOf(SettingsOps, name) >= 0)
            {
                return $"\"{name}\" is not an image edit (§10)";
            }
        }
        return null;
    }

    /// <summary>
    /// Check an option's <c>image</c> reference — exactly one of url / projectId / scanIndex —
    /// and warn that the bot shows no picture for any of them (§11.2). None resolves here: a
    /// <c>url</c> would have Telegram fetch a host nobody chose.
    /// </summary>
    private static void ValidateAskImage(JsonElement image, int index, List<string> warnings)
    {
        if (image.ValueKind != JsonValueKind.Object)
        {
            throw new PlanException($"ask option {index} \"image\" must be a JSON object");
        }
        string? only = null;
        int given = 0;
        foreach (JsonProperty property in image.EnumerateObject())
        {
            if (property.Name is not ("url" or "projectId" or "scanIndex"))
            {
                throw new PlanException($"ask option {index} \"image\" has an unknown field \"{property.Name}\"");
            }
            if (property.Value.ValueKind != JsonValueKind.Null)
            {
                given++;
                only = property.Name;
            }
        }
        if (given != 1)
        {
            throw new PlanException($"ask option {index} \"image\" needs exactly one of url, projectId, scanIndex");
        }
        if (only == "scanIndex")
        {
            // The extension's reference (§8) — meaningless here; the option stays pictureless.
            warnings.Add($"ask option {index} names a page-scan image, which this chat has no access to");
            return;
        }
        if (!image.TryGetProperty(only!, out JsonElement value)
            || value.ValueKind != JsonValueKind.String
            || value.GetString()?.Trim() is not string text
            || text.Length == 0)
        {
            throw new PlanException($"ask option {index} \"image.{only}\" must be a non-empty string");
        }
        warnings.Add(only == "projectId"
            ? $"ask option {index} names a stored project, which this chat can't preview"
            : $"ask option {index} names an image URL, which this chat does not fetch — the option is shown without a preview");
    }

    /// <summary>
    /// The text an answered card sends as the user's next turn: the picked labels joined, or the
    /// typed custom text, trimmed and capped (contract §11.3).
    /// </summary>
    public static string AskAnswerText(IEnumerable<string> pickedLabels, string? custom = null)
    {
        string typed = (custom ?? "").Trim();
        string answer = typed.Length > 0
            ? typed
            : string.Join(", ", pickedLabels.Where(static l => !string.IsNullOrWhiteSpace(l)).Select(static l => l.Trim()));
        return answer.Length > MaxAskAnswer ? answer[..MaxAskAnswer] : answer;
    }

    /// <summary>
    /// Parse an optional actions array (missing/null = none), bounded to 16 entries.
    /// <paramref name="inVariant"/> enforces §2.1's top-level-only ops.
    /// </summary>
    private static IReadOnlyList<PlanAction> ParseActionList(
        JsonElement parent, string name, List<string> warnings, bool inVariant = false)
    {
        if (!parent.TryGetProperty(name, out JsonElement array) || array.ValueKind == JsonValueKind.Null)
        {
            return [];
        }
        if (array.ValueKind != JsonValueKind.Array)
        {
            throw new PlanException($"\"{name}\" must be an array");
        }
        if (array.GetArrayLength() > MaxActions)
        {
            throw new PlanException($"too many actions (max {MaxActions})");
        }
        List<PlanAction> actions = new();
        foreach (JsonElement element in array.EnumerateArray())
        {
            PlanAction? action = ParseAction(element, warnings, inVariant);
            if (action is not null)
            {
                actions.Add(action);
            }
        }
        return actions;
    }

    /// <summary>One action: null when it carried an unknown op (skipped with a warning).</summary>
    private static PlanAction? ParseAction(JsonElement element, List<string> warnings, bool inVariant = false)
    {
        if (element.ValueKind != JsonValueKind.Object)
        {
            throw new PlanException("each action must be a JSON object");
        }
        if (!element.TryGetProperty("op", out JsonElement opElement)
            || opElement.ValueKind != JsonValueKind.String
            || opElement.GetString() is not string op
            || op.Length == 0)
        {
            throw new PlanException("an action is missing its \"op\" field");
        }
        // §2/§2.1: image/save/undo/redo/reset are top-level only — a variant exists to yield
        // one more take OF the working image, never to switch, persist or rewind one.
        if (inVariant && Array.IndexOf(TopLevelOnlyOps, op) >= 0)
        {
            throw new MisplacedOpException($"\"{op}\" is a top-level action only (§2.1)");
        }
        // §10: settings/connection ops are not image edits — they cost the variant its place.
        if (inVariant && Array.IndexOf(SettingsOps, op) >= 0)
        {
            throw new MisplacedOpException($"\"{op}\" is not an image edit (§10)");
        }
        switch (op)
        {
            case "crop": return ParseCrop(element);
            case "rotate": return ParseRotate(element);
            case "filter": return ParseFilter(element);
            case "layout": return ParseLayout(element);
            case "formula": return ParseFormula(element);
            case "page": return ParsePage(element);
            case "blank": return ParseBlank(element);
            case "frame": return ParseFrame(element);
            case "image": return ParseImage(element);
            case "save": return ParseSave(element);
            case "undo":
            case "redo": return ParseUndoRedo(element, op);
            case "reset": return ParseFieldless<ResetAction>(element, op);
            case "clear": return ParseFieldless<ClearAction>(element, op);
            case "clearChat": return ParseFieldless<ClearChatAction>(element, op);
            case "lineStyle": return ParseLineStyle(element);
            case "openUrl": return ParseOpenUrl(element);
            case "renameProject": return ParseRenameProject(element);
            case "describe": return ParseDescribe(element);
            case "blankColor": return ParseBlankColor(element);
            case "projectColor": return ParseProjectColor(element);
            case "export": return ParseExport(element);
            case "connect":
            case "disconnect": return ParseServerOp(element, op);
            default:
                // Forward compatibility: an unknown op is dropped, not fatal (contract §1).
                warnings.Add($"Skipped an unknown operation \"{op}\".");
                return null;
        }
    }

    /// <summary>Reject any property outside the op's allowed set (contract: unknown fields reject the action).</summary>
    private static void RequireOnly(JsonElement element, string op, params string[] allowed)
    {
        foreach (JsonProperty property in element.EnumerateObject())
        {
            if (property.Name != "op" && Array.IndexOf(allowed, property.Name) < 0)
            {
                throw new PlanException($"invalid \"{op}\" action: unexpected field \"{property.Name}\"");
            }
        }
    }

    private static string RequireString(JsonElement element, string op, string name)
    {
        if (!element.TryGetProperty(name, out JsonElement value) || value.ValueKind != JsonValueKind.String)
        {
            throw new PlanException($"invalid \"{op}\" action: \"{name}\" must be a string");
        }
        return value.GetString() ?? "";
    }
}
