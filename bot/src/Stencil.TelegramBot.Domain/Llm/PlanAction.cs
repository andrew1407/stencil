using Stencil.TelegramBot.Domain.Layout;

namespace Stencil.TelegramBot.Domain.Llm;

// The op-plan action union (llm-contract.md §2), discriminated on Op. Every variant maps 1:1
// onto an operation Stencil already has, so the executor calls the same code paths the slash
// commands do. No resize and no free-angle rotation, deliberately: core has neither.
// "top-level" below means the op is rejected inside a variant.
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

// The shared layout Line schema, in image-pixel coords.
public sealed record LayoutAction(IReadOnlyList<LayoutLine> Lines) : PlanAction
{
    public override string Op => "layout";
}

// Axis x/y with an Expr in that variable alone (empty clears the axis) — or Enabled alone,
// where false switches formulas off entirely, restoring identity.
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

// Indices holds the single `index` or the `indices` list.
public sealed record FrameAction(IReadOnlyList<int> Indices) : PlanAction
{
    public override string Op => "frame";
}

// §2.1, top-level: switch to the turn's Index-th attachment (1-based), resetting the §1
// coordinate frame.
public sealed record ImageAction(int Index) : PlanAction
{
    public override string Op => "image";
}

// §2.1, top-level: persist image + layout as a project — for the bot, its active server
// session. Path is the §10 destination, which the bot cannot honor: it saves to its usual
// place and says so.
public sealed record SaveAction(string? Name, string? Path = null) : PlanAction
{
    public override string Op => "save";
}

// §10, top-level: re-connect a server the user already SAVED with /connect. Resolved against
// the session's stored connections ONLY, and the stored token rides along — a plan never
// carries a token, and never names a host the user did not choose.
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

// §2, top-level: back to the original image (the bot's /reset).
public sealed record ResetAction : PlanAction
{
    public override string Op => "reset";
}

// §10, top-level: removes the working image and its edits ONLY — the conversation survives
// (clearing that is ClearChatAction, which is user-confirmed).
public sealed record ClearAction : PlanAction
{
    public override string Op => "clear";
}

// §10, top-level: the DEFAULT pen for new lines (/color /thickness /points /style /fill). Any
// subset. PointColor/DrawMode are §10 fields the bot's pen doesn't model — noted and skipped.
public sealed record LineStyleAction(
    string? Color, string? PointColor, int? Thickness, int? PointSize,
    string? Style, string? DrawMode, string? FillColor) : PlanAction
{
    public override string Op => "lineStyle";
}

// §10, top-level: the /url path with its SSRF vetting. The URL must appear VERBATIM in the
// user's OWN messages this conversation (the §10 user-echo guard, checked at execution) — a
// model-named host is never fetched. The executor awaits the load, so later actions see it.
public sealed record OpenUrlAction(string Url, bool Incognito) : PlanAction
{
    public override string Op => "openUrl";
}

// §10, top-level: /projectname.
public sealed record RenameProjectAction(string Name) : PlanAction
{
    public override string Op => "renameProject";
}

// §10, top-level: /projectdescription; ≤ 500 chars, "" clears.
public sealed record DescribeAction(string Text) : PlanAction
{
    public override string Op => "describe";
}

// §10, top-level: /blankcolor — recolours a BLANK project's background, keeping the edits.
public sealed record BlankColorAction(string Color) : PlanAction
{
    public override string Op => "blankColor";
}

// §10, top-level: /projectcolor; "" clears.
public sealed record ProjectColorAction(string Color) : PlanAction
{
    public override string Op => "projectColor";
}

// §10, top-level: What is layout (/json) or project (/project), sent into the user's OWN chat
// as a document — bounded to one send per action.
public sealed record ExportAction(string What) : PlanAction
{
    public override string Op => "export";
}

// §10, top-level: the /chat clear flow. Deferred to the END of the turn and confirmed in-app
// before anything is forgotten; a declined confirm is a note, never a failed plan.
public sealed record ClearChatAction : PlanAction
{
    public override string Op => "clearChat";
}
