using Stencil.TelegramBot.Domain.Layout;

namespace Stencil.TelegramBot.Domain.Llm;

// The op-plan action union (§2). No resize and no free-angle rotation, deliberately: core has
// neither. "top-level" below means the op is rejected inside a variant.
public abstract record PlanAction
{
    public abstract string Op { get; }
}

// The CLI's spec grammar, pre-joined as `x1=… x2=…` (the contract joins k=v with spaces).
public sealed record CropAction(string Spec) : PlanAction
{
    public override string Op => "crop";
}

// Quarter turns: Dir is left/right, Times 1..3.
public sealed record RotateAction(string Dir, int Times) : PlanAction
{
    public override string Op => "rotate";
}

// Mode is none/bw/sepia/invert/contour/custom; Tint rides custom only.
public sealed record FilterAction(string Mode, string? Tint) : PlanAction
{
    public override string Op => "filter";
}

public sealed record LayoutAction(IReadOnlyList<LayoutLine> Lines) : PlanAction
{
    public override string Op => "layout";
}

// Expr in Axis alone (empty clears the axis); Enabled=false switches formulas off entirely.
public sealed record FormulaAction(string? Axis, string? Expr, bool? Enabled = null) : PlanAction
{
    public override string Op => "formula";
}

// A lowercase ISO name (a0…c10) OR a custom WidthCm+HeightCm in centimetres — exactly one form.
public sealed record PageAction(string? Format, double? WidthCm = null, double? HeightCm = null) : PlanAction
{
    public override string Op => "page";
}

// Explicit WidthCm/HeightCm (both or neither) override Format.
public sealed record BlankAction(string Color, string? Format, double? WidthCm = null, double? HeightCm = null) : PlanAction
{
    public override string Op => "blank";
}

public sealed record FrameAction(IReadOnlyList<int> Indices) : PlanAction
{
    public override string Op => "frame";
}

// §2.1, top-level: the turn's Index-th attachment (1-based), resetting the §1 coordinate frame.
public sealed record ImageAction(int Index) : PlanAction
{
    public override string Op => "image";
}

// §2.1, top-level. Path is the §10 destination the bot cannot honor: it saves to its usual place.
public sealed record SaveAction(string? Name, string? Path = null) : PlanAction
{
    public override string Op => "save";
}

// §10, top-level: resolved against the session's stored connections ONLY, the stored token riding
// along — a plan never carries a token or names a host the user did not choose.
public sealed record ConnectAction(string Server) : PlanAction
{
    public override string Op => "connect";
}

// §10, top-level: resolved against stored connections, like ConnectAction.
public sealed record DisconnectAction(string Server) : PlanAction
{
    public override string Op => "disconnect";
}

// §2, top-level: Steps is 1..20.
public sealed record UndoAction(int Steps) : PlanAction
{
    public override string Op => "undo";
}

// §2, top-level: Steps is 1..20.
public sealed record RedoAction(int Steps) : PlanAction
{
    public override string Op => "redo";
}

public sealed record ResetAction : PlanAction
{
    public override string Op => "reset";
}

// §10, top-level: the working image and its edits ONLY; the conversation survives.
public sealed record ClearAction : PlanAction
{
    public override string Op => "clear";
}

// §10, top-level: the DEFAULT pen; PointColor/DrawMode are not modelled by the bot's pen.
public sealed record LineStyleAction(
    string? Color, string? PointColor, int? Thickness, int? PointSize,
    string? Style, string? DrawMode, string? FillColor) : PlanAction
{
    public override string Op => "lineStyle";
}

// §10, top-level: the /url path. The URL must appear VERBATIM in the user's OWN messages (the
// user-echo guard, checked at execution) — a model-named host is never fetched.
public sealed record OpenUrlAction(string Url, bool Incognito) : PlanAction
{
    public override string Op => "openUrl";
}

public sealed record RenameProjectAction(string Name) : PlanAction
{
    public override string Op => "renameProject";
}

// §10, top-level: /projectdescription; ≤ 500 chars, "" clears.
public sealed record DescribeAction(string Text) : PlanAction
{
    public override string Op => "describe";
}

public sealed record BlankColorAction(string Color) : PlanAction
{
    public override string Op => "blankColor";
}

// §10, top-level: /projectcolor; "" clears.
public sealed record ProjectColorAction(string Color) : PlanAction
{
    public override string Op => "projectColor";
}

// §10, top-level: sent into the user's OWN chat as a document, one send per action.
public sealed record ExportAction(string What) : PlanAction
{
    public override string Op => "export";
}

// §10, top-level: deferred to the END of the turn and confirmed in-app; a decline is a note.
public sealed record ClearChatAction : PlanAction
{
    public override string Op => "clearChat";
}
