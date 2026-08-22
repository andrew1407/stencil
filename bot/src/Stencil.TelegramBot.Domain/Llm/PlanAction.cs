using Stencil.TelegramBot.Domain.Layout;

namespace Stencil.TelegramBot.Domain.Llm;

/// <summary>
/// The op-plan action union (<c>llm-contract.md</c> §2), discriminated on <see cref="Op"/>.
/// Every variant maps 1:1 onto an operation Stencil already has — the executor calls the same
/// code paths the slash commands use. There is deliberately no resize and no free-angle
/// rotation (core has neither).
/// </summary>
public abstract record PlanAction
{
    public abstract string Op { get; }
}

/// <summary>Crop with the CLI's spec grammar, pre-joined as <c>x1=… x2=…</c> (contract: join k=v with spaces).</summary>
public sealed record CropAction(string Spec) : PlanAction
{
    public override string Op => "crop";
}

/// <summary>Quarter turns: <see cref="Dir"/> is <c>left</c>/<c>right</c>, <see cref="Times"/> 1..3.</summary>
public sealed record RotateAction(string Dir, int Times) : PlanAction
{
    public override string Op => "rotate";
}

/// <summary><see cref="Mode"/> is none/bw/sepia/invert/contour/custom; <see cref="Tint"/> rides <c>custom</c> only.</summary>
public sealed record FilterAction(string Mode, string? Tint) : PlanAction
{
    public override string Op => "filter";
}

/// <summary>Draw annotation polylines (the shared layout Line schema, image-pixel coords).</summary>
public sealed record LayoutAction(IReadOnlyList<LayoutLine> Lines) : PlanAction
{
    public override string Op => "layout";
}

/// <summary>
/// Coordinate transform: <see cref="Axis"/> is <c>x</c>/<c>y</c> with <see cref="Expr"/> using
/// only that variable (empty = clear that axis) — or <see cref="Enabled"/> alone, where false
/// switches formulas OFF entirely (restoring identity).
/// </summary>
public sealed record FormulaAction(string? Axis, string? Expr, bool? Enabled = null) : PlanAction
{
    public override string Op => "formula";
}

/// <summary>
/// Set the page format: a lowercase ISO name (<c>a0</c>…<c>c10</c>), OR a custom size as
/// <see cref="WidthCm"/>+<see cref="HeightCm"/> centimetres (exactly one of the two forms).
/// </summary>
public sealed record PageAction(string? Format, double? WidthCm = null, double? HeightCm = null) : PlanAction
{
    public override string Op => "page";
}

/// <summary>
/// Create a blank page: a fill colour plus an optional page format; explicit centimetre dims
/// ride as <see cref="WidthCm"/>/<see cref="HeightCm"/> (both or neither) and override the format.
/// </summary>
public sealed record BlankAction(string Color, string? Format, double? WidthCm = null, double? HeightCm = null) : PlanAction
{
    public override string Op => "blank";
}

/// <summary>Pick video frame(s); <see cref="Indices"/> holds the single <c>index</c> or the <c>indices</c> list.</summary>
public sealed record FrameAction(IReadOnlyList<int> Indices) : PlanAction
{
    public override string Op => "frame";
}

/// <summary>
/// §2.1: switch the working image to the turn's <see cref="Index"/>-th attached image (1-based,
/// attachment order), resetting the §1 coordinate frame. Top-level actions only.
/// </summary>
public sealed record ImageAction(int Index) : PlanAction
{
    public override string Op => "image";
}

/// <summary>
/// §2.1: persist the current image + layout as a project — for the bot, its active server
/// session. <see cref="Name"/> is optional (≤ 120 chars). <see cref="Path"/> is the §10
/// destination (≤ 1024 chars, trimmed, never a URL) — the bot saves to its usual place
/// and notes it cannot honor a path. Top-level actions only.
/// </summary>
public sealed record SaveAction(string? Name, string? Path = null) : PlanAction
{
    public override string Op => "save";
}

/// <summary>
/// §10 (carried into the bot profile): re-connect a server the user already SAVED with
/// <c>/connect</c>. <see cref="Server"/> is resolved against the session's stored
/// connections only — the stored token rides along; plans never carry tokens. Top-level
/// actions only.
/// </summary>
public sealed record ConnectAction(string Server) : PlanAction
{
    public override string Op => "connect";
}

/// <summary>§10 (bot profile): forget a connection, resolved the same way. Top-level actions only.</summary>
public sealed record DisconnectAction(string Server) : PlanAction
{
    public override string Op => "disconnect";
}

/// <summary>§2: step this surface's edit history back <see cref="Steps"/> entries (1..20). Top-level only.</summary>
public sealed record UndoAction(int Steps) : PlanAction
{
    public override string Op => "undo";
}

/// <summary>§2: re-apply <see cref="Steps"/> undone entries (1..20). Top-level only.</summary>
public sealed record RedoAction(int Steps) : PlanAction
{
    public override string Op => "redo";
}

/// <summary>§2: drop every pending edit back to the original image (the bot's <c>/reset</c>). Top-level only.</summary>
public sealed record ResetAction : PlanAction
{
    public override string Op => "reset";
}

/// <summary>
/// §10 (bot profile): REMOVE the working image and its edits, scoped to the image and edits
/// only — the assistant conversation survives (clearing it is <see cref="ClearChatAction"/>,
/// user-confirmed). Top-level only.
/// </summary>
public sealed record ClearAction : PlanAction
{
    public override string Op => "clear";
}

/// <summary>
/// §10 (bot profile): change the DEFAULT pen for newly drawn lines — the <c>/color</c>
/// <c>/thickness</c> <c>/points</c> <c>/style</c> <c>/fill</c> paths. Any subset of fields;
/// <see cref="PointColor"/>/<see cref="DrawMode"/> are §10 fields the bot's pen doesn't model
/// (noted and skipped at execution). Top-level only.
/// </summary>
public sealed record LineStyleAction(
    string? Color, string? PointColor, int? Thickness, int? PointSize,
    string? Style, string? DrawMode, string? FillColor) : PlanAction
{
    public override string Op => "lineStyle";
}

/// <summary>
/// §10 (bot profile): load an image from an http(s) URL as the working image — the <c>/url</c>
/// path with its SSRF vetting. The URL must appear VERBATIM in the user's own messages of this
/// conversation (the §10 user-echo guard — checked at execution, plan-level). The executor
/// awaits the load, so later actions act on the fetched picture. Top-level only.
/// </summary>
public sealed record OpenUrlAction(string Url, bool Incognito) : PlanAction
{
    public override string Op => "openUrl";
}

/// <summary>§10 (bot profile): rename the active project / relabel the working image (<c>/projectname</c>). Top-level only.</summary>
public sealed record RenameProjectAction(string Name) : PlanAction
{
    public override string Op => "renameProject";
}

/// <summary>§10 (bot profile): set the project description, ≤ 500 chars, <c>""</c> clears (<c>/projectdescription</c>). Top-level only.</summary>
public sealed record DescribeAction(string Text) : PlanAction
{
    public override string Op => "describe";
}

/// <summary>§10 (bot profile): recolour a BLANK project's background, keeping the edits (<c>/blankcolor</c>). Top-level only.</summary>
public sealed record BlankColorAction(string Color) : PlanAction
{
    public override string Op => "blankColor";
}

/// <summary>§10 (bot profile): the project's name colour, <c>""</c> clears (<c>/projectcolor</c>). Top-level only.</summary>
public sealed record ProjectColorAction(string Color) : PlanAction
{
    public override string Op => "projectColor";
}

/// <summary>
/// §10 (bot profile): send the layout JSON (<c>/json</c>) or the portable <c>.stencil</c>
/// project (<c>/project</c>) into the user's own chat as a document — bounded to ONE send per
/// action. <see cref="What"/> is <c>layout</c>/<c>project</c>. Top-level only.
/// </summary>
public sealed record ExportAction(string What) : PlanAction
{
    public override string Op => "export";
}

/// <summary>
/// §10 (carried by every chat surface): clear THIS conversation's history — the bot's
/// <c>/chat clear</c> flow. Deferred to the END of the turn and confirmed in-app before
/// anything is forgotten (a declined confirm is a note, never a failed plan). Top-level only.
/// </summary>
public sealed record ClearChatAction : PlanAction
{
    public override string Op => "clearChat";
}
