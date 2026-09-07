using System.Text;
using System.Text.RegularExpressions;

namespace Stencil.TelegramBot.Application.Llm;

/// <summary>
/// One §13 op-registry entry: the op name(s) a single prompt bullet documents (two ops share
/// one bullet when the prompt presents them together, e.g. undo/redo), the bullet text
/// VERBATIM, and the op's flags — <see cref="Profile"/> for the bot's §10 profile block vs
/// §4's core "Available ops" list, <see cref="TopLevelOnly"/> for §2/§2.1's variant ban, and
/// an optional <see cref="Capability"/> tag naming the runtime capability the op needs (an
/// entry whose capability is not wired on this surface is excluded from prompt generation).
/// </summary>
public sealed record OpDescriptor(
    IReadOnlyList<string> Names,
    string Bullet,
    bool Profile = false,
    bool TopLevelOnly = false,
    string? Capability = null);

/// <summary>
/// The bot's §13 op registry — the single source of an op's existence. Every op the bot
/// executes has exactly one entry carrying its prompt bullet (read from the shared
/// <c>opRegistry.json</c> via <see cref="OpSchema"/>) and flags; the §4 ops section and the
/// §10 bot profile block are ASSEMBLED from these entries (never hand-embedded), the
/// parser's variant bans derive from the flags, and tests cross-check the parser's dispatch
/// against <see cref="Names"/>. Assembly refuses bullets matching sensitive patterns and
/// excludes entries whose capability is not wired, so the prompt can never promise an op the
/// surface cannot run — nor leak configuration language into the model's instructions.
/// </summary>
public static partial class OpRegistry
{
    /// <summary>
    /// Every op the bot executes, in prompt order: the §2 core ops first (the §4 "Available
    /// ops" list), then the §10 bot profile block spliced at the op list's end. Bullets come
    /// verbatim from the shared registry (<see cref="OpSchema"/>) — the flags here are the
    /// bot's own policy (prompt block, variant ban, wiring).
    /// </summary>
    public static readonly IReadOnlyList<OpDescriptor> Ops =
    [
        Entry(["crop"]),
        Entry(["rotate"]),
        Entry(["filter"]),
        Entry(["layout"]),
        Entry(["formula"]),
        Entry(["page"]),
        Entry(["blank"]),
        Entry(["undo", "redo"], TopLevelOnly: true),
        Entry(["frame"]),
        Entry(["image"], TopLevelOnly: true),
        Entry(["save"], TopLevelOnly: true),
        Entry(["connect", "disconnect"], Profile: true, Capability: "server-connections"),
        Entry(["reset"], Profile: true, TopLevelOnly: true),
        Entry(["clear"], Profile: true),
        Entry(["lineStyle"], Profile: true),
        Entry(["openUrl"], Profile: true, Capability: "url-fetch"),
        Entry(["renameProject"], Profile: true),
        Entry(["describe"], Profile: true),
        Entry(["blankColor"], Profile: true),
        Entry(["projectColor"], Profile: true),
        Entry(["export"], Profile: true),
        Entry(["clearChat"], Profile: true, TopLevelOnly: true),
    ];

    /// <summary>
    /// One descriptor with its bullet read from the shared registry (the bot variant when one
    /// is recorded); a second name must ride the first's bullet (<c>bulletSharedWith</c>).
    /// </summary>
    private static OpDescriptor Entry(string[] names, bool Profile = false, bool TopLevelOnly = false, string? Capability = null)
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
            Profile, TopLevelOnly, Capability);
    }

    /// <summary>
    /// The runtime capabilities actually wired on this surface. An entry tagged with a
    /// capability outside this set is EXCLUDED from prompt generation (§13): the op falls to
    /// §1's unknown-op skip and the model was never promised it.
    /// </summary>
    public static readonly IReadOnlyCollection<string> WiredCapabilities =
        new HashSet<string>(StringComparer.Ordinal) { "server-connections", "url-fetch", "chat-documents" };

    /// <summary>
    /// §13's never-model-drivable boundary for this surface: llm/provider configuration,
    /// clipboard reads, hotkey rebinding, session end, chat persistence/consent toggles, and
    /// server-side destruction beyond what §10 grants the bot — the registry's
    /// <c>forbidden.perSurface.bot</c> list. Two teeth: a test asserts no registry entry uses
    /// one of these names, and the executor rejects a plan carrying one even if a slip ever
    /// let it parse.
    /// </summary>
    public static readonly IReadOnlyCollection<string> ForbiddenOps = OpSchema.Bot.Forbidden;

    /// <summary>Every registered op name, flattened in prompt order.</summary>
    public static readonly IReadOnlyList<string> Names =
        Ops.SelectMany(static o => o.Names).ToArray();

    /// <summary>The §2/§2.1 top-level-only op names — the parser's variant/preview ban list.</summary>
    public static readonly string[] TopLevelOnlyNames =
        Ops.Where(static o => o.TopLevelOnly).SelectMany(static o => o.Names).ToArray();

    /// <summary>
    /// The §10-scoped settings/profile op names (not image edits — banned inside variants).
    /// <c>reset</c> (a §2 op) and <c>clearChat</c> ride the profile BLOCK but are policed as
    /// top-level-only instead.
    /// </summary>
    public static readonly string[] SettingsNames =
        Ops.Where(static o => o.Profile && !o.TopLevelOnly).SelectMany(static o => o.Names).ToArray();

    /// <summary>The generated §4 "Available ops" section: the core (non-profile) bullets.</summary>
    public static string CoreOpsSection { get; } =
        BuildSection(Ops.Where(static o => !o.Profile), WiredCapabilities);

    /// <summary>The generated §10 bot profile bullets (spliced at the op list's end).</summary>
    public static string ProfileOpsSection { get; } =
        BuildSection(Ops.Where(static o => o.Profile), WiredCapabilities);

    /// <summary>
    /// Assemble one prompt section from registry entries: bullets joined with a newline, in
    /// registry order. Entries whose <see cref="OpDescriptor.Capability"/> is not in
    /// <paramref name="wired"/> are excluded (§13); a bullet matching a sensitive pattern
    /// (api keys, bearer tokens, endpoint-setting instructions) throws — a registry mistake
    /// fails loudly at assembly instead of leaking into the prompt.
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
