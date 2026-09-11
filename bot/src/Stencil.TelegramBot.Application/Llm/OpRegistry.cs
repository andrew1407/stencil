using System.Text;
using System.Text.RegularExpressions;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Application.Llm;

/// <summary>
/// One §13 op-registry entry: the op name(s) one prompt bullet documents (undo/redo share
/// theirs), that bullet VERBATIM, the flags — <see cref="Profile"/> for §10's block vs §4's
/// core list, <see cref="TopLevelOnly"/> for the variant ban, <see cref="Capability"/> for the
/// wiring an op needs — and the <see cref="Handler"/> that EXECUTES it. Membership, prompt and
/// dispatch are one structure, the shape <c>pystencil</c>'s <c>OpSpec</c> already has.
/// </summary>
public sealed record OpDescriptor(
    IReadOnlyList<string> Names,
    string Bullet,
    bool Profile = false,
    bool TopLevelOnly = false,
    string? Capability = null,
    OpHandler? Handler = null);

/// <summary>
/// The bot's §13 op registry — the single source of an op's existence. One entry per op
/// carries its bullet (from the shared <c>opRegistry.json</c> via <see cref="OpSchema"/>), its
/// flags AND its applier: the §4/§10 prompt sections are ASSEMBLED from these entries, the
/// parser's variant bans derive from the flags, and <see cref="PromptService"/> dispatches
/// through <see cref="HandlerFor"/> — so the prompt cannot promise an op nothing executes, and
/// nothing executes an op the registry never listed. Assembly refuses sensitive bullets and
/// excludes unwired capabilities.
/// </summary>
public static partial class OpRegistry
{
    /// <summary>
    /// Every op the bot executes, in prompt order: the §2 core ops, then the §10 profile block.
    /// Bullets are the shared registry's verbatim; the flags and appliers are the bot's own.
    /// </summary>
    public static readonly IReadOnlyList<OpDescriptor> Ops =
    [
        Entry(["crop"], (s, a, c, ct) => s.ApplyCropAsync(c, (CropAction)a, ct)),
        Entry(["rotate"], (s, a, c, ct) => s.ApplyRotateAsync(c, (RotateAction)a, ct)),
        Entry(["filter"], (s, a, c, ct) => s.ApplyFilterAsync(c, (FilterAction)a, ct)),
        Entry(["layout"], (s, a, c, ct) => s.ApplyLayoutAsync(c, (LayoutAction)a, ct)),
        Entry(["formula"], (s, a, c, ct) => s.ApplyFormulaAsync(c, (FormulaAction)a, ct)),
        Entry(["page"], (s, a, c, ct) => s.ApplyPageAsync(c, (PageAction)a, ct)),
        Entry(["blank"], (s, a, c, ct) => s.ApplyBlankAsync(c, (BlankAction)a, ct)),
        Entry(["undo", "redo"], (s, a, c, ct) => s.StepHistoryAsync(c, a, ct), TopLevelOnly: true),
        Entry(["frame"], (s, a, c, ct) => s.ApplyFrameAsync(c, (FrameAction)a, ct)),
        Entry(["image"], (s, a, c, ct) => s.SwitchImageAsync(c, (ImageAction)a, ct), TopLevelOnly: true),
        Entry(["save"], (s, a, c, ct) => s.SaveProjectAsync(c, (SaveAction)a, ct), TopLevelOnly: true),
        Entry(["connect", "disconnect"], (s, a, c, ct) => s.ChangeConnectionAsync(c, a, ct),
            Profile: true, Capability: "server-connections"),
        Entry(["reset"], (s, a, c, ct) => s.ApplyResetAsync(c, ct), Profile: true, TopLevelOnly: true),
        Entry(["clear"], (s, a, c, ct) => s.ClearImageAsync(c, ct), Profile: true),
        Entry(["lineStyle"], (s, a, c, ct) => s.ConfigurePenAsync(c, (LineStyleAction)a, ct), Profile: true),
        Entry(["openUrl"], (s, a, c, ct) => s.OpenUrlAsync(c, (OpenUrlAction)a, ct),
            Profile: true, Capability: "url-fetch"),
        Entry(["renameProject"], (s, a, c, ct) => s.RenameProjectAsync(c, (RenameProjectAction)a, ct), Profile: true),
        Entry(["describe"], (s, a, c, ct) => s.DescribeProjectAsync(c, (DescribeAction)a, ct), Profile: true),
        Entry(["blankColor"], (s, a, c, ct) => s.SetBlankColorAsync(c, (BlankColorAction)a, ct), Profile: true),
        Entry(["projectColor"], (s, a, c, ct) => s.SetProjectColorAsync(c, (ProjectColorAction)a, ct), Profile: true),
        Entry(["export"], (s, a, c, ct) => s.ExportAsync(c, (ExportAction)a, ct), Profile: true),
        // §10 clearChat executes nothing here — ExecuteAsync surfaces the deferred request on
        // the outcome once the plan's other actions are done.
        Entry(["clearChat"], static (s, a, c, ct) => Task.CompletedTask, Profile: true, TopLevelOnly: true),
    ];

    /// <summary>The applier for one op name — the executor's only way in.</summary>
    public static OpHandler? HandlerFor(string op) =>
        ByName.TryGetValue(op, out OpDescriptor? entry) ? entry.Handler : null;

    private static readonly IReadOnlyDictionary<string, OpDescriptor> ByName =
        Ops.SelectMany(static o => o.Names.Select(n => (Name: n, Entry: o)))
            .ToDictionary(static x => x.Name, static x => x.Entry, StringComparer.Ordinal);

    /// <summary>
    /// The registry-edit guard <c>pystencil</c> runs at import: every op the shared registry
    /// lists for this surface is bound to an applier here, and no forbidden name is registered.
    /// A registry edit that forgets one fails loudly at startup instead of at the first plan.
    /// </summary>
    static OpRegistry()
    {
        string[] unbound = [.. OpSchema.Bot.Ops.Keys
            .Where(name => !ByName.TryGetValue(name, out OpDescriptor? e) || e.Handler is null)
            .Order(StringComparer.Ordinal)];
        if (unbound.Length > 0)
        {
            throw new InvalidOperationException(
                $"opRegistry.json registers {string.Join(", ", unbound)} for the bot, but nothing here executes them");
        }
        string[] forbidden = [.. ByName.Keys.Where(ForbiddenOps.Contains).Order(StringComparer.Ordinal)];
        if (forbidden.Length > 0)
        {
            throw new InvalidOperationException(
                $"forbidden ops may never be registered: {string.Join(", ", forbidden)}");
        }
    }

    /// <summary>One descriptor, bullet read from the shared registry; a second name must ride
    /// the first's bullet (<c>bulletSharedWith</c>).</summary>
    private static OpDescriptor Entry(
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

    /// <summary>The capabilities wired here. An entry tagged outside this set is EXCLUDED from
    /// the prompt (§13) and falls to §1's unknown-op skip.</summary>
    public static readonly IReadOnlyCollection<string> WiredCapabilities =
        new HashSet<string>(StringComparer.Ordinal) { "server-connections", "url-fetch", "chat-documents" };

    /// <summary>
    /// §13's never-model-drivable boundary (the registry's <c>forbidden.perSurface.bot</c>):
    /// llm/provider config, clipboard reads, hotkey rebinding, session end, chat
    /// persistence toggles, server-side destruction beyond §10's grants. Two teeth — the
    /// static ctor below, and the executor's reject.
    /// </summary>
    public static readonly IReadOnlyCollection<string> ForbiddenOps = OpSchema.Bot.Forbidden;

    /// <summary>Every registered op name, flattened in prompt order.</summary>
    public static readonly IReadOnlyList<string> Names =
        Ops.SelectMany(static o => o.Names).ToArray();

    /// <summary>The §2/§2.1 top-level-only op names — the parser's variant/preview ban list.</summary>
    public static readonly string[] TopLevelOnlyNames =
        Ops.Where(static o => o.TopLevelOnly).SelectMany(static o => o.Names).ToArray();

    /// <summary>The §10 settings op names (banned inside variants). <c>reset</c> and
    /// <c>clearChat</c> ride the block but are policed as top-level-only instead.</summary>
    public static readonly string[] SettingsNames =
        Ops.Where(static o => o.Profile && !o.TopLevelOnly).SelectMany(static o => o.Names).ToArray();

    /// <summary>The generated §4 "Available ops" section: the core (non-profile) bullets.</summary>
    public static string CoreOpsSection { get; } =
        BuildSection(Ops.Where(static o => !o.Profile), WiredCapabilities);

    /// <summary>The generated §10 bot profile bullets (spliced at the op list's end).</summary>
    public static string ProfileOpsSection { get; } =
        BuildSection(Ops.Where(static o => o.Profile), WiredCapabilities);

    /// <summary>
    /// Assemble one prompt section: bullets newline-joined in registry order, minus entries
    /// whose capability is not <paramref name="wired"/> (§13). A bullet matching a sensitive
    /// pattern throws — a registry mistake fails loudly instead of leaking into the prompt.
    /// </summary>
    public static string BuildSection(IEnumerable<OpDescriptor> ops, IReadOnlyCollection<string> wired)
    {
        StringBuilder sb = new();
        foreach (OpDescriptor op in ops)
        {
            if (op.Capability is string capability && !wired.Contains(capability))
            {
                continue;   // not wired here — the model is never promised it
            }
            if (SensitiveBullet().IsMatch(op.Bullet))
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

    // The §13 prompt censor. Deliberately context-sensitive on "token": the crop bullet's
    // "edge tokens" are legitimate; an auth/api/bearer/server token is not.
    [GeneratedRegex(@"(?i)\bapi[\s_-]?key|\bbearer\b|\bauthorization\b|\b(auth\w*|access|secret|server|api)[\s_-]?token|\bendpoint")]
    private static partial Regex SensitiveBullet();
}
