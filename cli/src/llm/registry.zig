//! §13 op registry + §4 system-prompt assembly for the console LLM assistant:
//! the executor-side op descriptors (Action tag, bullet, capability — the validating
//! side is the embedded registry, opSchema.zig), the forbidden-op/censor teeth, and
//! the lazily assembled canonical + console prompts (systemPrompt.json).
const std = @import("std");
const opplan = @import("opplan.zig");
const opSchema = @import("opSchema.zig");
const transport = @import("transport.zig");

// Symbols living in the sibling llm/ modules (facade: ../llm.zig).
const Action = opplan.Action;
const findOp = opplan.findOp;

// The canonical system prompt (contract §4 + §13, registry-generated ops)
//
// §4's two-part rule: the PROSE CORE is embedded verbatim; the ops list is GENERATED from
// `op_registry` — the same table the validator's variant gates read — so the prompt can
// never promise an op this console cannot run (§13: one registry entry per op).

/// §13 capability tags: an op whose runtime capability is not wired into a build is
/// excluded from prompt assembly, so it falls to §1's unknown-op skip and was never
/// promised. The shipped console wires all of them (`full_capabilities`).
pub const OpCapability = enum { theme, network, filesystem, clipboard };
pub const OpCaps = std.EnumSet(OpCapability);
pub const full_capabilities = OpCaps.initFull();

/// One §13 descriptor: an op's wire name, its `Action` tag, its prompt bullet(s)
/// VERBATIM and its capability tag. Its key schema and variant-gate flags live in
/// opRegistry.json (opSchema.zig). A comptime check pins the table 1:1 onto `Action`.
pub const OpDescriptor = struct {
    name: []const u8,
    tag: std.meta.Tag(Action),
    /// The §4 "Available ops" bullet. Null when a sibling's bullet already covers
    /// the op (`redo` rides `undo`'s) or the op is parsed for §2 compatibility but
    /// never advertised (`reset`).
    bullet: ?[]const u8 = null,
    /// The console settings-block bullet (the CLI's §10-analog profile).
    console_bullet: ?[]const u8 = null,
    /// A console-block line riding AFTER the op bullets (crop's `album` spec key).
    console_addendum: ?[]const u8 = null,
    /// The runtime capability the op needs, when it needs one (§13 exclusion).
    capability: ?OpCapability = null,
};

/// The op registry (§13). Table order IS prompt order: core §2 ops first (their
/// bullets form §4's "Available ops" section), then the console profile (its bullets
/// form the settings block spliced in by `consoleSystemPrompt()`).
pub const op_registry = [_]OpDescriptor{
    .{
        .name = "crop",
        .tag = .crop,
        .bullet =
        \\- {"op":"crop","spec":{"x1":"10%","x2":"-10%","aspect":"3:4"}} — move edges inward;
        \\  tokens are numbers with optional unit % / px / cm / in; a leading "-" measures from the
        \\  opposite side. Include only the edges you want to move. For a target aspect ratio add
        \\  "aspect":"W:H" INSIDE "spec", never beside it (portrait "3:4", album/landscape "4:3",
        \\  square "1:1") — the editor cuts the resolved crop to that exact ratio about its centre,
        \\  so NEVER derive ratio tokens yourself; combine it with edge tokens when a specific
        \\  region should be kept.
        ,
        .console_addendum =
        \\- The crop op's "spec" also takes "album": true — derive the missing crop axis from
        \\  the page format in landscape orientation (the console's '/crop … album').
        ,
    },
    .{
        .name = "rotate",
        .tag = .rotate,
        .bullet =
        \\- {"op":"rotate","dir":"left"|"right","times":1..3} — quarter turns only.
        ,
    },
    .{
        .name = "filter",
        .tag = .filter,
        .bullet =
        \\- {"op":"filter","mode":"none"|"bw"|"sepia"|"invert"|"contour"|"custom","tint":"#rrggbb"}
        \\  — "custom" is a duotone tint and requires "tint"; "contour" is edge detection.
        ,
    },
    .{
        .name = "layout",
        .tag = .layout,
        .bullet =
        \\- {"op":"layout","lines":[{"points":[{"x":0,"y":0},...],"color":"#FFFF00","thickness":2,
        \\  "pointSize":4,"style":"solid"|"dashed"|"dotted","locked":false,"fillColor":"transparent"}]}
        \\  — draw annotation polylines in image-pixel coordinates. When asked to extract lines,
        \\  shapes, or structure from an attached image, answer with this op. An empty "lines"
        \\  array REMOVES every drawn line — that is what "clear/remove the lines" means.
        ,
    },
    .{
        .name = "formula",
        .tag = .formula,
        .bullet =
        \\- {"op":"formula","axis":"x"|"y","expr":"x*2+10"} — coordinate transform; single variable
        \\  matching the axis; operators + - * / ** and parentheses only. An empty "expr" clears
        \\  that axis; {"op":"formula","enabled":false} switches formulas OFF entirely.
        ,
    },
    .{
        .name = "page",
        .tag = .page,
        .bullet =
        \\- {"op":"page","format":"a4"} — ISO page formats a0–a10, b0–b10, c0–c10 — or a custom
        \\  size: {"op":"page","width":20,"height":30} in centimetres (one form or the other).
        ,
    },
    .{
        .name = "blank",
        .tag = .blank,
        .bullet =
        \\- {"op":"blank","color":"#ffffff","format":"a4"} — create a blank page; explicit
        \\  centimetre dims ride as "width"/"height" instead of "format".
        ,
    },
    .{
        .name = "undo",
        .tag = .undo,
        .bullet =
        \\- {"op":"undo","steps":1} / {"op":"redo","steps":1} — step this surface's edit history.
        \\  "Undo that" means {"op":"undo"}; steps count history entries, which can be finer
        \\  than one request.
        ,
    },
    // redo rides undo's bullet; reset is parsed per §2 but never advertised.
    .{ .name = "redo", .tag = .redo },
    .{ .name = "reset", .tag = .reset },
    .{
        .name = "frame",
        .tag = .frame,
        .bullet =
        \\- {"op":"frame","index":0} or {"op":"frame","indices":[0,30,60]} — pick video frame(s);
        \\  only valid when the current input is a video.
        ,
    },
    .{
        .name = "image",
        .tag = .image,
        .bullet =
        \\- {"op":"image","index":1} — switch the working image to the Nth image attached to THIS
        \\  message (1-based, in attachment order); coordinates in later actions are in THAT
        \\  image's pixel frame. Only valid when the user attached images. Use it to edit several
        \\  attached images in one plan, giving each image its OWN actions.
        ,
    },
    .{
        .name = "save",
        .tag = .save,
        .bullet =
        \\- {"op":"save","name":"portrait 1","path":"~/Downloads"} — save the current image with its
        \\  drawn lines. `path` is optional and may be a folder or a file name (".stencil" saves the
        \\  whole project, an image extension saves the picture); with no path it writes a project
        \\  into the working directory. ONLY a path the user themselves wrote in this conversation —
        \\  never invent, complete or rewrite one. When the user asks to process several images and
        \\  keep the results, finish each image's actions with a "save" before switching to the
        \\  next: image 1, its edits, save, image 2, its edits, save, …
        ,
    },
    // The console settings-op profile (the CLI's §10 analog)
    // Ops that drive the console's OWN controls, the way the GUI editors' §10 block
    // drives theirs. Every op maps 1:1 onto an EXISTING console command (/theme,
    // /connect, /disconnect, /reconnect, /delete, /upload <url>, /copy, /drop).
    .{
        .name = "accent",
        .tag = .accent,
        .capability = .theme,
        .console_bullet =
        \\- {"op":"accent","color":"#7c3aed"} — set the console's colour theme (its accent).
        \\  "color" must be a #rrggbb hex, so translate colour names yourself (cyan =
        \\  "#00ffff"). The console has no light/dark mode — a theme request means this op.
        \\- "accent" also accepts {"op":"accent","preset":"green"} — one of the console's
        \\  named /theme presets; use a preset when the user names a colour that has one.
        ,
    },
    .{
        .name = "connect",
        .tag = .connect,
        .capability = .network,
        .console_bullet =
        \\- {"op":"connect","server":"..."} / {"op":"disconnect","server":"..."} — manage the
        \\  user's collaboration-server connections. Only a server listed in the console
        \\  state below may be named — never invent, complete, or suggest a new address; for
        \\  a server not listed there, tell the user to run '/connect <url>' themselves.
        ,
    },
    // disconnect rides connect's bullet.
    .{ .name = "disconnect", .tag = .disconnect, .capability = .network },
    .{
        .name = "reconnect",
        .tag = .reconnect,
        .capability = .network,
        .console_bullet =
        \\- {"op":"reconnect","server":"..."} — re-establish a connection that went stale
        \\  (the console's /reconnect); the same server rule as connect.
        ,
    },
    .{
        .name = "delete",
        .tag = .delete,
        .capability = .filesystem,
        .console_bullet =
        \\- {"op":"delete","path":"old.stencil"} — delete a LOCAL .stencil project file in
        \\  the working directory (the console's /delete). Only .stencil files, never a URL
        \\  or a path outside the working directory.
        ,
    },
    .{
        .name = "openFile",
        .tag = .open_file,
        .capability = .filesystem,
        .console_bullet =
        \\- {"op":"openFile","path":"~/Pictures/portrait.png"} — load a LOCAL file the user named
        \\  as the working image: an image or video, a layout ".json" (drawn onto the current
        \\  picture), or a ".stencil" project. ONLY a path the user themselves wrote in this
        \\  conversation — never invent, complete, guess or list one, and never a directory.
        ,
    },
    .{
        .name = "openUrl",
        .tag = .open_url,
        .capability = .network,
        .console_bullet =
        \\- {"op":"openUrl","url":"https://…"} — load an image (or video frame) from a URL
        \\  as the working image (the console's /upload). ONLY a URL the user themselves
        \\  wrote in this conversation — never introduce, complete, or rewrite one.
        ,
    },
    .{
        .name = "copy",
        .tag = .copy,
        .capability = .clipboard,
        .console_bullet =
        \\- {"op":"copy"} — copy the current rendered image to the system clipboard. This IS
        \\  what "copy the result / copy to clipboard" means; never answer that it cannot be
        \\  done. Takes no fields.
        ,
    },
    .{
        .name = "clear",
        .tag = .clear,
        .console_bullet =
        \\- {"op":"clear"} — REMOVE the working image and its lines, leaving the editor empty.
        \\  This is what "remove/delete/clear the image" means. Never answer that with
        \\  {"op":"blank"}: a blank REPLACES the picture with a white page, which is not a
        \\  removal. Takes no fields.
        ,
    },
    .{
        .name = "clearChat",
        .tag = .clear_chat,
        .console_bullet =
        \\- {"op":"clearChat"} — clear THIS conversation's history; the app asks the user to
        \\  confirm first, and the clear happens after this plan's other actions finish. This IS
        \\  what "clear the chat / conversation / history" means; never answer that it cannot be
        \\  done. Takes no fields.
        ,
    },
};

// One entry per `Action` variant, each exactly once, and never a forbidden name.
comptime {
    @setEvalBranchQuota(100_000);
    const tags = @typeInfo(std.meta.Tag(Action)).@"enum".fields;
    if (op_registry.len != tags.len)
        @compileError("op_registry must carry exactly one entry per Action variant");
    for (tags) |t| {
        var hits: usize = 0;
        for (op_registry) |d| {
            if (std.mem.eql(u8, @tagName(d.tag), t.name)) hits += 1;
        }
        if (hits != 1) @compileError("op_registry must name Action." ++ t.name ++ " exactly once");
    }
}

/// §13 forbidden ops — the "never model-drivable" boundary, the registry's
/// `forbidden.perSurface.cli`. Two teeth: no descriptor may use one of these names
/// (tested below) and the validator's outright reject in `validateActions`.
pub fn isForbiddenOp(op: []const u8) bool {
    return opSchema.get().isForbidden(op);
}

/// §13 prompt censor: patterns no generated bullet may match — a registry mistake fails
/// at assembly (`checkCensor`'s @compileError). Deliberately NOT a bare "token": the crop
/// bullet's spec "tokens" are innocent; these patterns name credentials.
pub const sensitive_patterns = [_][]const u8{
    "api key",  "api-key",       "apikey",  "api_key",
    "bearer",   "authorization", "secret",  "endpoint",
    "base url", "base_url",      "baseurl",
};

/// True when `text` matches a §13 sensitive pattern (case-insensitive). Works at
/// comptime (the generator's censor) and runtime (the parity test's assertions).
pub fn bulletIsSensitive(text: []const u8) bool {
    for (sensitive_patterns) |p| {
        if (std.ascii.findIgnoreCasePosLinear(text, 0, p) != null) return true;
    }
    return false;
}

fn checkCensor(comptime bullet: []const u8) []const u8 {
    if (bulletIsSensitive(bullet))
        @compileError("§13 censor: a registry bullet matches a sensitive pattern");
    return bullet;
}

fn opIncluded(comptime d: OpDescriptor, comptime caps: OpCaps) bool {
    return d.capability == null or caps.contains(d.capability.?);
}

/// Assemble §4's "Available ops" section from the registry (§13): each included op's
/// bullet, table order, each preceded by the newline that separates it from the
/// header / previous bullet. Excluded capabilities simply drop their bullets.
pub fn opsSection(comptime caps: OpCaps) []const u8 {
    comptime {
        @setEvalBranchQuota(1_000_000);
        var out: []const u8 = "";
        for (op_registry) |d| {
            if (d.bullet != null and opIncluded(d, caps)) out = out ++ "\n" ++ checkCensor(d.bullet.?);
        }
        return out;
    }
}

/// Assemble the console settings block from the registry (§13): the included console
/// bullets in table order, then the addenda (crop's `album` key), then the footer.
pub fn consoleBlock(comptime caps: OpCaps) []const u8 {
    comptime {
        @setEvalBranchQuota(1_000_000);
        var out: []const u8 = "";
        for (op_registry) |d| {
            if (d.console_bullet != null and opIncluded(d, caps))
                out = out ++ (if (out.len == 0) "" else "\n") ++ checkCensor(d.console_bullet.?);
        }
        for (op_registry) |d| {
            if (d.console_addendum != null and opIncluded(d, caps))
                out = out ++ "\n" ++ checkCensor(d.console_addendum.?);
        }
        return out ++ "\n" ++ console_footer;
    }
}

const console_footer = "These console ops are not image edits and cannot appear inside \"variants\".";

// §4's prose core comes VERBATIM from the embedded shared asset (systemPrompt.json);
// the ops list is generated (§13), and the `ask` paragraph is this surface's ONE tail
// divergence (numbered, no previews). Parsed lazily — std.json needs an allocator.
const prompt_asset_json = @embedFile("systemPrompt.json");

// The console's own `ask` paragraph (replaces the asset tail's up to the shared anchor).
const console_ask =
    \\When a choice is genuinely the user's to make — which tint, which of several images —
    \\add an "ask" object instead of guessing:
    \\{"ask":{"question":"Which tint?","mode":"single"|"multi","allowCustom":true,
    \\  "options":[{"label":"Sepia"},{"label":"B&W"}]}}
    \\2 to 5 options; the user's pick comes back as their next message. This console shows the
    \\options as a numbered list and cannot display pictures, so make each label say enough on
    \\its own. Never write your own "Something else" / "Other" option: set "allowCustom": true and the client appends that free-text row itself.
;

// Where the shared tail prose begins, both in the asset and in the assembled prompt.
const tail_shared_anchor = "\n\nOutlining (";

const ops_section = opsSection(full_capabilities);

var prompt_scratch: [24 * 1024]u8 = undefined; // decoded head+tail (≈6.2KB) + parser slack
var prompt_storage: [40 * 1024]u8 = undefined; // assembled §4 + console prompts (≈21KB)
var assembled_system: []const u8 = &.{};
var assembled_console: []const u8 = &.{};

fn assemblePrompts() void {
    const Doc = struct { head: []const u8, tail: []const u8 };
    var fba = std.heap.FixedBufferAllocator.init(&prompt_scratch);
    const doc = std.json.parseFromSliceLeaky(Doc, fba.allocator(), prompt_asset_json, .{
        .ignore_unknown_fields = true, // extensionHead is the extension's business
    }) catch @panic("embedded systemPrompt.json is malformed");
    // Fail-fast pins: this is the contract-§4 asset, not some other file.
    if (doc.head.len != 1197 or doc.tail.len != 4930)
        @panic("embedded systemPrompt.json: unexpected head/tail length");
    if (!std.mem.startsWith(u8, doc.head, "You are the AI assistant inside Stencil, an image-annotation tool."))
        @panic("embedded systemPrompt.json: head lost the §4 first sentence");
    if (doc.head[doc.head.len - 1] != '\n' or !std.mem.startsWith(u8, doc.tail, "\n\n"))
        @panic("embedded systemPrompt.json: head/tail edges moved");
    const shared_at = std.mem.indexOf(u8, doc.tail, tail_shared_anchor) orelse
        @panic("embedded systemPrompt.json: tail no longer contains the Outlining anchor");
    // head's trailing '\n' is the separator ops_section already leads with — drop it.
    var w: usize = 0;
    for ([_][]const u8{ doc.head[0 .. doc.head.len - 1], ops_section, "\n\n", console_ask, doc.tail[shared_at..] }) |part| {
        if (w + part.len > prompt_storage.len) @panic("system prompt overflows its storage");
        @memcpy(prompt_storage[w..][0..part.len], part);
        w += part.len;
    }
    const system = prompt_storage[0..w];
    // Splice the console block at the end of §4's op list (the browser's
    // SETTINGS_SPLICE_ANCHOR); a §4 rewording fails loudly here rather than
    // silently shipping a prompt without the block.
    const i = std.mem.indexOf(u8, system, console_splice_anchor) orelse
        @panic("system prompt no longer contains the console-settings splice anchor");
    const start = w;
    for ([_][]const u8{ system[0..i], "\n", console_settings_prompt, system[i..] }) |part| {
        if (w + part.len > prompt_storage.len) @panic("console prompt overflows its storage");
        @memcpy(prompt_storage[w..][0..part.len], part);
        w += part.len;
    }
    assembled_system = system;
    assembled_console = prompt_storage[start..w];
}

/// The canonical §4 system prompt: prose core verbatim from the shared asset, ops
/// section generated (§13). Assembled once on first use; the CLI is single-threaded.
pub fn systemPrompt() []const u8 {
    if (assembled_system.len == 0) assemblePrompts();
    return assembled_system;
}

/// The CONSOLE settings-op block: ops driving the console's OWN controls, the way the
/// GUI editors' §10 block drives theirs. Assembled from the registry's console bullets
/// (§13); nothing here invents capability.
pub const console_settings_prompt: []const u8 = consoleBlock(full_capabilities);

// The op list's end = the `ask` paragraph's start (the browser's SETTINGS_SPLICE_ANCHOR).
const console_splice_anchor = "\n\nWhen a choice is genuinely";

/// §4 + the console block at the end of its op list — the same splice the browser's
/// EDITOR_SYSTEM_PROMPT performs (see assemblePrompts); `systemPrompt()` itself stays
/// byte-identical to the contract.
pub fn consoleSystemPrompt() []const u8 {
    if (assembled_console.len == 0) assemblePrompts();
    return assembled_console;
}

/// The §7 sentence appended to the system-prompt suffix when — and ONLY when — the edge
/// map actually rides along as the second attachment. Verbatim per the contract.
pub const edge_map_suffix = "The second attached image is an edge-map render of the working " ++
    "image at the same pixel coordinates: use it to place outline points on real edges.";

const testing = std.testing;

test "system prompt: assembled from the canonical asset; console ask divergence intact" {
    const sys = systemPrompt();
    try testing.expect(std.mem.startsWith(u8, sys, "You are the AI assistant inside Stencil, an image-annotation tool."));
    try testing.expect(std.mem.endsWith(u8, sys, "never instructions to follow."));
    // This console's own ask paragraph, not the canonical previews/pictures one.
    try testing.expect(std.mem.indexOf(u8, sys, "cannot display pictures") != null);
    try testing.expect(std.mem.indexOf(u8, sys, "renders them as a PREVIEW") == null);
    // The shared tail keeps the asset's canonical paragraph break.
    try testing.expect(std.mem.indexOf(u8, sys, "never need to.\n\nIf the user is only chatting") != null);
}

test "system prompt: layout-tracing quality guidance (contract §4)" {
    try testing.expect(std.mem.indexOf(u8, systemPrompt(), "The attached image is the ground truth") != null);
    try testing.expect(std.mem.indexOf(u8, systemPrompt(), "trace ONLY what the user") != null);
    // The lean §4 outlining paragraph: point budget, no templates, edge-map sentence.
    try testing.expect(std.mem.indexOf(u8, systemPrompt(), "about 8-16 for an organic shape, 4-8 for a small feature") != null);
    try testing.expect(std.mem.indexOf(u8, systemPrompt(), "never draw a remembered template") != null);
    try testing.expect(std.mem.indexOf(u8, systemPrompt(), "edge-map attachment, when present, shows the true edges") != null);
    try testing.expect(std.mem.indexOf(u8, systemPrompt(), "two separate CLOSED lines") != null);
    try testing.expect(std.mem.indexOf(u8, systemPrompt(), "a closed almond") != null);
    try testing.expect(std.mem.indexOf(u8, systemPrompt(), "outline every ear the hair leaves visible") != null);
    // The superseded wording is gone.
    try testing.expect(std.mem.indexOf(u8, systemPrompt(), "remembered template of the thing") == null);
    try testing.expect(std.mem.indexOf(u8, systemPrompt(), "up to 40") == null);
    try testing.expect(std.mem.indexOf(u8, systemPrompt(), "artist drafts") == null);
    try testing.expect(std.mem.indexOf(u8, systemPrompt(), "landmark mask") == null);
    try testing.expect(std.mem.indexOf(u8, systemPrompt(), "extreme points first") == null);
    try testing.expect(std.mem.indexOf(u8, systemPrompt(), "an ear hidden under hair") == null);
}

test "console prompt: the settings block splices after the op list; §4 stays canonical" {
    // The block sits between the op list's last entry and the `ask` paragraph…
    const block = std.mem.indexOf(u8, consoleSystemPrompt(), console_settings_prompt).?;
    const save_line = std.mem.indexOf(u8, consoleSystemPrompt(), "image 2, its edits, save,").?;
    const ask_para = std.mem.indexOf(u8, consoleSystemPrompt(), "When a choice is genuinely").?;
    try testing.expect(save_line < block and block < ask_para);
    // …and the canonical §4 constant itself carries none of it.
    try testing.expect(std.mem.indexOf(u8, systemPrompt(), "\"op\":\"accent\"") == null);
    try testing.expect(std.mem.indexOf(u8, systemPrompt(), "\"op\":\"connect\"") == null);
    // Everything around the splice is §4 verbatim: removing the block gives §4 back.
    const before = consoleSystemPrompt()[0..std.mem.indexOf(u8, consoleSystemPrompt(), "\n" ++ console_settings_prompt).?];
    const after = consoleSystemPrompt()[block + console_settings_prompt.len ..];
    try testing.expect(std.mem.startsWith(u8, systemPrompt(), before));
    try testing.expect(std.mem.endsWith(u8, systemPrompt(), after));
}

test "§13 registry: op name set matches the contract's cli-console profile" {
    // Core §2 ops (incl. §2.1 image/save and the history ops) + the cli-console
    // profile — table order IS prompt order (the registry's console order), nothing
    // more, nothing less, and never a forbidden name.
    const expected = [_][]const u8{
        "crop",    "rotate",     "filter",    "layout", "formula",  "page",    "blank",
        "undo",    "redo",       "reset",     "frame",  "image",    "save",    "accent",
        "connect", "disconnect", "reconnect", "delete", "openFile", "openUrl", "copy",
        "clear",   "clearChat",
    };
    try testing.expect(op_registry.len == expected.len);
    try testing.expect(op_registry.len == std.meta.fields(std.meta.Tag(Action)).len);
    for (expected, op_registry) |name, d| try testing.expectEqualStrings(name, d.name);
    const schema = opSchema.get();
    try testing.expectEqual(op_registry.len, schema.entries.len);
    for (schema.entries, op_registry) |e, d| try testing.expectEqualStrings(e.name, d.name);
    for (op_registry) |d| try testing.expect(!isForbiddenOp(d.name));
}

test "§13 registry: flags match the contract (top-level-only, console scope, capabilities)" {
    // The variant gates read the registry's flags: topLevelOnly (§2/§2.1) and the
    // settings ops (§10's editorSetting / the console's consoleSetting).
    const top_level = [_][]const u8{ "image", "save", "undo", "redo", "reset", "openFile" };
    const console = [_][]const u8{ "accent", "connect", "disconnect", "reconnect", "delete", "openFile", "openUrl", "copy", "clear", "clearChat" };
    for (op_registry) |d| {
        const e = opSchema.get().find(d.name).?;
        var want_top = false;
        for (top_level) |n| want_top = want_top or std.mem.eql(u8, n, d.name);
        try testing.expectEqual(want_top, e.top_level_only);
        var want_console = false;
        for (console) |n| want_console = want_console or std.mem.eql(u8, n, d.name);
        try testing.expectEqual(want_console, e.settings);
        // Only console ops carry capability tags (core §2 ops are always wired).
        if (d.capability != null) try testing.expect(e.settings);
    }
    try testing.expect(opSchema.get().find("clearChat").?.deferred);
    try testing.expect(findOp("accent").?.capability.? == .theme);
    try testing.expect(findOp("delete").?.capability.? == .filesystem);
    try testing.expect(findOp("openFile").?.capability.? == .filesystem);
    try testing.expect(findOp("copy").?.capability.? == .clipboard);
    inline for (.{ "connect", "disconnect", "reconnect", "openUrl" }) |n|
        try testing.expect(findOp(n).?.capability.? == .network);
    try testing.expect(findOp("clear").?.capability == null); // dropping state needs nothing
    try testing.expect(findOp("clearChat").?.capability == null); // local state + an existing connection
}

test "§13 registry: one key semantic phrase per bullet" {
    const pins = [_]struct { op: []const u8, phrase: []const u8 }{
        .{ .op = "crop", .phrase = "move edges inward" },
        .{ .op = "rotate", .phrase = "quarter turns only" },
        .{ .op = "filter", .phrase = "\"custom\" is a duotone tint" },
        .{ .op = "layout", .phrase = "array REMOVES every drawn line" },
        .{ .op = "formula", .phrase = "switches formulas OFF entirely" },
        .{ .op = "page", .phrase = "in centimetres (one form or the other)" },
        .{ .op = "blank", .phrase = "create a blank page" },
        .{ .op = "undo", .phrase = "step this surface's edit history" },
        .{ .op = "frame", .phrase = "only valid when the current input is a video" },
        .{ .op = "image", .phrase = "in attachment order" },
        .{ .op = "save", .phrase = "ONLY a path the user themselves wrote" },
        .{ .op = "accent", .phrase = "set the console's colour theme" },
        .{ .op = "connect", .phrase = "never invent, complete, or suggest a new address" },
        .{ .op = "reconnect", .phrase = "re-establish a connection that went stale" },
        .{ .op = "delete", .phrase = "Only .stencil files, never a URL" },
        .{ .op = "openFile", .phrase = "never invent, complete, guess or list one" },
        .{ .op = "openUrl", .phrase = "ONLY a URL the user themselves" },
        .{ .op = "copy", .phrase = "never answer that it cannot be" },
        .{ .op = "clear", .phrase = "REMOVE the working image and its lines" },
        .{ .op = "clearChat", .phrase = "clear THIS conversation's history" },
    };
    for (pins) |pin| {
        const d = findOp(pin.op).?;
        const bullet = d.bullet orelse d.console_bullet.?;
        try testing.expect(std.mem.indexOf(u8, bullet, pin.phrase) != null);
    }
    // The accent bullet also promises its preset form (§10's two accent shapes).
    try testing.expect(std.mem.indexOf(u8, findOp("accent").?.console_bullet.?, "{\"op\":\"accent\",\"preset\":\"green\"}") != null);
    // redo rides undo's bullet; reset is parsed per §2 but never advertised.
    try testing.expect(findOp("redo").?.bullet == null and findOp("redo").?.console_bullet == null);
    try testing.expect(findOp("reset").?.bullet == null and findOp("reset").?.console_bullet == null);
    // crop's console addendum carries the album spec key — console block only.
    try testing.expect(std.mem.indexOf(u8, findOp("crop").?.console_addendum.?, "\"album\": true") != null);
    try testing.expect(std.mem.indexOf(u8, systemPrompt(), "\"album\": true") == null);
    try testing.expect(std.mem.indexOf(u8, systemPrompt(), "\"op\":\"clear\"") == null);
    try testing.expect(std.mem.indexOf(u8, systemPrompt(), "\"op\":\"reconnect\"") == null);
    try testing.expect(std.mem.indexOf(u8, systemPrompt(), "\"op\":\"clearChat\"") == null);
    // The clearChat bullet also promises the confirm and the end-of-turn deferral (§10).
    const cc = findOp("clearChat").?.console_bullet.?;
    try testing.expect(std.mem.indexOf(u8, cc, "asks the user to") != null);
    try testing.expect(std.mem.indexOf(u8, cc, "after this plan's other actions finish") != null);
    try testing.expect(std.mem.indexOf(u8, cc, "Takes no fields") != null);
}

test "§13 capability exclusion: an unwired capability drops its bullet from assembly" {
    const no_clipboard = comptime blk: {
        var caps = full_capabilities;
        caps.remove(.clipboard);
        break :blk caps;
    };
    const block = comptime consoleBlock(no_clipboard);
    try testing.expect(std.mem.indexOf(u8, block, "\"op\":\"copy\"") == null);
    try testing.expect(std.mem.indexOf(u8, block, "\"op\":\"clear\"") != null);
    // Dropping network loses all four connection-flavoured bullets at once.
    const offline = comptime blk: {
        var caps = full_capabilities;
        caps.remove(.network);
        break :blk caps;
    };
    const off = comptime consoleBlock(offline);
    inline for (.{ "connect", "disconnect", "reconnect", "openUrl" }) |n|
        try testing.expect(std.mem.indexOf(u8, off, "\"op\":\"" ++ n ++ "\"") == null);
    try testing.expect(std.mem.indexOf(u8, off, "\"op\":\"accent\"") != null);
    // The full set reproduces the shipped block and ops section exactly.
    try testing.expectEqualStrings(console_settings_prompt, comptime consoleBlock(full_capabilities));
    try testing.expect(std.mem.indexOf(u8, systemPrompt(), comptime opsSection(full_capabilities)) != null);
}

test "§13 censor: no emitted bullet matches a sensitive pattern; the predicate bites" {
    for (op_registry) |d| {
        if (d.bullet) |b| try testing.expect(!bulletIsSensitive(b));
        if (d.console_bullet) |b| try testing.expect(!bulletIsSensitive(b));
        if (d.console_addendum) |b| try testing.expect(!bulletIsSensitive(b));
    }
    try testing.expect(bulletIsSensitive("- {\"op\":\"key\"} — set the API key"));
    try testing.expect(bulletIsSensitive("ride the Bearer credential along"));
    try testing.expect(bulletIsSensitive("point the ENDPOINT at http://x"));
    // crop's spec "tokens" stay innocent — the patterns name credentials, not the word.
    try testing.expect(!bulletIsSensitive("tokens are numbers with optional unit"));
}
