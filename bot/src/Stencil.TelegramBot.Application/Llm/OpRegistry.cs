using System.Text;
using System.Text.RegularExpressions;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Application.Llm;

// One §13 entry: name(s), the bullet VERBATIM, flags, and the handler that EXECUTES it —
// pystencil's OpSpec shape.
public sealed record OpDescriptor(
    IReadOnlyList<string> Names,
    string Bullet,
    bool Profile = false,
    bool TopLevelOnly = false,
    string? Capability = null,
    OpHandler? Handler = null);

// The §13 registry: the prompt sections are ASSEMBLED from these entries and the executor
// dispatches through HandlerFor, so the prompt cannot promise an op nothing executes, and nothing
// executes an unlisted op.
public static partial class OpRegistry
{
    // Prompt order: the §2 core ops, then the §10 profile block.
    public static readonly IReadOnlyList<OpDescriptor> Ops =
    [
        makeEntry(["crop"], (s, a, c, ct) => s.ApplyCropAsync(c, (CropAction)a, ct)),
        makeEntry(["rotate"], (s, a, c, ct) => s.ApplyRotateAsync(c, (RotateAction)a, ct)),
        makeEntry(["filter"], (s, a, c, ct) => s.ApplyFilterAsync(c, (FilterAction)a, ct)),
        makeEntry(["layout"], (s, a, c, ct) => s.ApplyLayoutAsync(c, (LayoutAction)a, ct)),
        makeEntry(["formula"], (s, a, c, ct) => s.ApplyFormulaAsync(c, (FormulaAction)a, ct)),
        makeEntry(["page"], (s, a, c, ct) => s.ApplyPageAsync(c, (PageAction)a, ct)),
        makeEntry(["blank"], (s, a, c, ct) => s.ApplyBlankAsync(c, (BlankAction)a, ct)),
        makeEntry(["undo", "redo"], (s, a, c, ct) => s.StepHistoryAsync(c, a, ct), TopLevelOnly: true),
        makeEntry(["frame"], (s, a, c, ct) => s.ApplyFrameAsync(c, (FrameAction)a, ct)),
        makeEntry(["image"], (s, a, c, ct) => s.SwitchImageAsync(c, (ImageAction)a, ct), TopLevelOnly: true),
        makeEntry(["save"], (s, a, c, ct) => s.SaveProjectAsync(c, (SaveAction)a, ct), TopLevelOnly: true),
        makeEntry(["connect", "disconnect"], (s, a, c, ct) => s.ChangeConnectionAsync(c, a, ct),
            Profile: true, Capability: "server-connections"),
        makeEntry(["reset"], (s, a, c, ct) => s.ApplyResetAsync(c, ct), Profile: true, TopLevelOnly: true),
        makeEntry(["clear"], (s, a, c, ct) => s.ClearImageAsync(c, ct), Profile: true),
        makeEntry(["lineStyle"], (s, a, c, ct) => s.ConfigurePenAsync(c, (LineStyleAction)a, ct), Profile: true),
        makeEntry(["openUrl"], (s, a, c, ct) => s.OpenUrlAsync(c, (OpenUrlAction)a, ct),
            Profile: true, Capability: "url-fetch"),
        makeEntry(["renameProject"], (s, a, c, ct) => s.RenameProjectAsync(c, (RenameProjectAction)a, ct), Profile: true),
        makeEntry(["describe"], (s, a, c, ct) => s.DescribeProjectAsync(c, (DescribeAction)a, ct), Profile: true),
        makeEntry(["blankColor"], (s, a, c, ct) => s.SetBlankColorAsync(c, (BlankColorAction)a, ct), Profile: true),
        makeEntry(["projectColor"], (s, a, c, ct) => s.SetProjectColorAsync(c, (ProjectColorAction)a, ct), Profile: true),
        makeEntry(["export"], (s, a, c, ct) => s.ExportAsync(c, (ExportAction)a, ct), Profile: true),
        // §10 clearChat executes nothing here; ExecuteAsync surfaces the deferred request on the
        // outcome.
        makeEntry(["clearChat"], static (s, a, c, ct) => Task.CompletedTask, Profile: true, TopLevelOnly: true),
    ];

    public static OpHandler? HandlerFor(string op) =>
        _byName.TryGetValue(op, out OpDescriptor? entry) ? entry.Handler : null;

    private static readonly IReadOnlyDictionary<string, OpDescriptor> _byName =
        Ops.SelectMany(static o => o.Names.Select(n => (Name: n, Entry: o)))
            .ToDictionary(static x => x.Name, static x => x.Entry, StringComparer.Ordinal);

    // pystencil's import-time guard: every op the shared registry lists for this surface must be
    // bound here.
    static OpRegistry()
    {
        string[] unbound = [.. OpSchema.Bot.Ops.Keys
            .Where(name => !_byName.TryGetValue(name, out OpDescriptor? e) || e.Handler is null)
            .Order(StringComparer.Ordinal)];
        if (unbound.Length > 0)
        {
            throw new InvalidOperationException(
                $"opRegistry.json registers {string.Join(", ", unbound)} for the bot, but nothing here executes them");
        }
        string[] forbidden = [.. _byName.Keys.Where(ForbiddenOps.Contains).Order(StringComparer.Ordinal)];
        if (forbidden.Length > 0)
        {
            throw new InvalidOperationException(
                $"forbidden ops may never be registered: {string.Join(", ", forbidden)}");
        }
    }

    // A second name must ride the first's bullet (bulletSharedWith).
    private static OpDescriptor makeEntry(
        string[] names, OpHandler handler, bool Profile = false, bool TopLevelOnly = false, string? Capability = null)
    {
        OpEntry lead = OpSchema.Bot.Ops[names[0]];
        foreach (string name in names.Skip(1))
        {
            if (OpSchema.Bot.Ops[name].BulletSharedWith != names[0])
            {
                throw new InvalidOperationException($"op \"{name}\" does not share \"{names[0]}\"'s bullet in the registry");
            }
        }
        return new(names, lead.Bullet ?? throw new InvalidOperationException($"op \"{names[0]}\" has no bullet in the registry"),
            Profile, TopLevelOnly, Capability, handler);
    }

    // An entry tagged outside this set is EXCLUDED from the prompt (§13) and falls to §1's
    // unknown-op skip.
    public static readonly IReadOnlyCollection<string> WiredCapabilities =
        new HashSet<string>(StringComparer.Ordinal) { "server-connections", "url-fetch", "chat-documents" };

    // §13's never-model-drivable boundary (forbidden.perSurface.bot); the static ctor and the
    // executor both refuse.
    public static readonly IReadOnlyCollection<string> ForbiddenOps = OpSchema.Bot.Forbidden;

    public static readonly IReadOnlyList<string> Names =
        Ops.SelectMany(static o => o.Names).ToArray();

    public static readonly string[] TopLevelOnlyNames =
        Ops.Where(static o => o.TopLevelOnly).SelectMany(static o => o.Names).ToArray();

    // reset and clearChat ride the §10 block but are policed as top-level-only instead.
    public static readonly string[] SettingsNames =
        Ops.Where(static o => o.Profile && !o.TopLevelOnly).SelectMany(static o => o.Names).ToArray();

    public static string CoreOpsSection { get; } =
        BuildSection(Ops.Where(static o => !o.Profile), WiredCapabilities);

    public static string ProfileOpsSection { get; } =
        BuildSection(Ops.Where(static o => o.Profile), WiredCapabilities);

    // A bullet matching a sensitive pattern throws: a registry mistake fails loudly instead of
    // leaking into the prompt.
    public static string BuildSection(IEnumerable<OpDescriptor> ops, IReadOnlyCollection<string> wired)
    {
        StringBuilder sb = new();
        foreach (OpDescriptor op in ops)
        {
            if (op.Capability is string capability && !wired.Contains(capability))
            {
                continue;   // not wired here — the model is never promised it
            }
            if (sensitiveBullet().IsMatch(op.Bullet))
            {
                throw new InvalidOperationException(
                    $"op \"{op.Names[0]}\": its prompt bullet matches a sensitive pattern (api key / bearer / token / endpoint) — refusing to assemble the prompt");
            }
            if (sb.Length > 0)
            {
                sb.Append('\n');
            }
            sb.Append(op.Bullet);
        }
        return sb.ToString();
    }

    // The §13 prompt censor; "token" is context-sensitive — the crop bullet's "edge tokens" are
    // legitimate.
    [GeneratedRegex(@"(?i)\bapi[\s_-]?key|\bbearer\b|\bauthorization\b|\b(auth\w*|access|secret|server|api)[\s_-]?token|\bendpoint")]
    private static partial Regex sensitiveBullet();
}
