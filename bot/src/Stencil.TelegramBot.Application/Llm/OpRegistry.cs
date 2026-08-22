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
/// executes has exactly one entry carrying its prompt bullet and flags; the §4 ops section
/// and the §10 bot profile block are ASSEMBLED from these entries (never hand-embedded), the
/// parser's variant bans derive from the flags, and tests cross-check the parser's dispatch
/// against <see cref="Names"/>. Assembly refuses bullets matching sensitive patterns and
/// excludes entries whose capability is not wired, so the prompt can never promise an op the
/// surface cannot run — nor leak configuration language into the model's instructions.
/// </summary>
public static partial class OpRegistry
{
    /// <summary>
    /// Every op the bot executes, in prompt order: the §2 core ops first (the §4 "Available
    /// ops" list), then the §10 bot profile block spliced at the op list's end. Bullets are
    /// verbatim — moving one here must never change its bytes.
    /// </summary>
    public static readonly IReadOnlyList<OpDescriptor> Ops =
    [
        new(["crop"],
            """
            - {"op":"crop","spec":{"x1":"10%","x2":"-10%","aspect":"3:4"}} — move edges inward;
              tokens are numbers with optional unit % / px / cm / in; a leading "-" measures from the
              opposite side. Include only the edges you want to move. For a target aspect ratio add
              "aspect":"W:H" INSIDE "spec", never beside it (portrait "3:4", album/landscape "4:3",
              square "1:1") — the editor cuts the resolved crop to that exact ratio about its centre,
              so NEVER derive ratio tokens yourself; combine it with edge tokens when a specific
              region should be kept.
            """),
        new(["rotate"],
            """
            - {"op":"rotate","dir":"left"|"right","times":1..3} — quarter turns only.
            """),
        new(["filter"],
            """
            - {"op":"filter","mode":"none"|"bw"|"sepia"|"invert"|"contour"|"custom","tint":"#rrggbb"}
              — "custom" is a duotone tint and requires "tint"; "contour" is edge detection.
            """),
        new(["layout"],
            """
            - {"op":"layout","lines":[{"points":[{"x":0,"y":0},...],"color":"#FFFF00","thickness":2,
              "pointSize":4,"style":"solid"|"dashed"|"dotted","locked":false,"fillColor":"transparent"}]}
              — draw annotation polylines in image-pixel coordinates. When asked to extract lines,
              shapes, or structure from an attached image, answer with this op. An empty "lines"
              array REMOVES every drawn line — that is what "clear/remove the lines" means.
            """),
        new(["formula"],
            """
            - {"op":"formula","axis":"x"|"y","expr":"x*2+10"} — coordinate transform; single variable
              matching the axis; operators + - * / ** and parentheses only. An empty "expr" clears
              that axis; {"op":"formula","enabled":false} switches formulas OFF entirely.
            """),
        new(["page"],
            """
            - {"op":"page","format":"a4"} — ISO page formats a0–a10, b0–b10, c0–c10 — or a custom
              size: {"op":"page","width":20,"height":30} in centimetres (one form or the other).
            """),
        new(["blank"],
            """
            - {"op":"blank","color":"#ffffff","format":"a4"} — create a blank page; explicit
              centimetre dims ride as "width"/"height" instead of "format".
            """),
        new(["undo", "redo"],
            """
            - {"op":"undo","steps":1} / {"op":"redo","steps":1} — step this surface's edit history.
              "Undo that" means {"op":"undo"}; steps count history entries, which can be finer
              than one request.
            """,
            TopLevelOnly: true),
        new(["frame"],
            """
            - {"op":"frame","index":0} or {"op":"frame","indices":[0,30,60]} — pick video frame(s);
              only valid when the current input is a video.
            """),
        new(["image"],
            """
            - {"op":"image","index":1} — switch the working image to the Nth image attached to THIS
              message (1-based, in attachment order); coordinates in later actions are in THAT
              image's pixel frame. Only valid when the user attached images. Use it to edit several
              attached images in one plan, giving each image its OWN actions.
            """,
            TopLevelOnly: true),
        new(["save"],
            """
            - {"op":"save","name":"portrait 1"} — save the current image with its drawn lines as a
              project. When the user asks to process several images and keep the results, finish
              each image's actions with a "save" before switching to the next: image 1, its edits,
              save, image 2, its edits, save, …
            """,
            TopLevelOnly: true),
        new(["connect", "disconnect"],
            """
            - {"op":"connect","server":"..."} / {"op":"disconnect","server":"..."} — manage the
              user's collaboration-server connections. Only a server the user has already saved
              may be named — never invent or suggest a new address.
            """,
            Profile: true,
            Capability: "server-connections"),
        new(["reset"],
            """
            - {"op":"reset"} — drop EVERY pending edit and go back to the original image (the
              /reset command). "Start over with this picture" means THIS. Takes no fields.
            """,
            Profile: true,
            TopLevelOnly: true),
        new(["clear"],
            """
            - {"op":"clear"} — REMOVE the working image and its edits, leaving the chat with no
              working image; the conversation itself is kept. This is what "remove/delete/clear
              the image" means. Never answer that with {"op":"blank"}: a blank REPLACES the
              picture with a white page, which is not a removal. Takes no fields.
            """,
            Profile: true),
        new(["lineStyle"],
            """
            - {"op":"lineStyle","color":"#00ff00","thickness":3,"pointSize":6,"style":"dashed",
              "fillColor":"transparent"} — change the DEFAULT pen for newly drawn lines (any
              subset of fields), like the /color /thickness /points /style /fill commands.
            """,
            Profile: true),
        new(["openUrl"],
            """
            - {"op":"openUrl","url":"https://…"} — load an image (or video frame) from a URL as
              the working image (the /url command). ONLY a URL the user themselves wrote in this
              conversation — never introduce, complete, or rewrite one.
            """,
            Profile: true,
            Capability: "url-fetch"),
        new(["renameProject"],
            """
            - {"op":"renameProject","name":"…"} — rename the active server project (or relabel
              a not-yet-saved working image).
            """,
            Profile: true),
        new(["describe"],
            """
            - {"op":"describe","text":"…"} — set the project's free-text description (up to 500
              characters); "" clears it.
            """,
            Profile: true),
        new(["blankColor"],
            """
            - {"op":"blankColor","color":"#dbeafe"} — recolour a BLANK project's background,
              KEEPING the drawn lines. "Recolour/change the background" means THIS, never a new
              {"op":"blank"} (that replaces the page and destroys the lines).
            """,
            Profile: true),
        new(["projectColor"],
            """
            - {"op":"projectColor","color":"#ec4899"} — the project's name colour ("" = clear).
            """,
            Profile: true),
        new(["export"],
            """
            - {"op":"export","what":"layout"|"project"} — send the layout JSON or the portable
              .stencil project file into this chat as a document.
            """,
            Profile: true,
            Capability: "chat-documents"),
        new(["clearChat"],
            """
            - {"op":"clearChat"} — clear THIS conversation's history; the app asks the user to
              confirm first, and the clear happens after this plan's other actions finish. This IS
              what "clear the chat / conversation / history" means; never answer that it cannot be
              done. Takes no fields.
            """,
            Profile: true,
            TopLevelOnly: true),
    ];

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
    /// server-side destruction beyond what §10 grants the bot. Two teeth: a test asserts no
    /// registry entry uses one of these names, and the executor rejects a plan carrying one
    /// even if a slip ever let it parse.
    /// </summary>
    public static readonly IReadOnlyList<string> ForbiddenOps =
    [
        "llm", "provider", "apiKey",                      // assistant self-configuration
        "paste",                                          // clipboard reads
        "hotkey", "hotkeys",                              // hotkey rebinding
        "quit", "exit",                                   // session/window end
        "chat", "shareTabs",                              // chat persistence / consent toggles (clearing is the distinct clearChat op)
        "delete", "removeProject", "clearProjects", "expire", // server-side destruction beyond §10
    ];

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
