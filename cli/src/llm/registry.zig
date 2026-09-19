//! §13 op registry + §4 system-prompt assembly for the console LLM assistant:
//! the executor-side op descriptors (Action tag, bullet, capability — the validating
//! side is the embedded registry, opSchema.zig), the forbidden-op/censor teeth, and
//! the lazily assembled canonical + console prompts (systemPrompt.json).
const std = @import("std");
const opplan = @import("opplan.zig");
const opSchema = @import("opSchema.zig");
const transport = @import("transport.zig");

const descriptor = @import("registry/descriptor.zig");
const table = @import("registry/table.zig");
const coreOps = @import("registry/coreOps.zig");
const consoleOps = @import("registry/consoleOps.zig");
const prompt = @import("registry/prompt.zig");

const Action = opplan.Action;
const findOp = opplan.findOp;

pub const OpCapability = descriptor.OpCapability;
pub const OpCaps = descriptor.OpCaps;
pub const full_capabilities = descriptor.full_capabilities;
pub const OpDescriptor = descriptor.OpDescriptor;
pub const isForbiddenOp = descriptor.isForbiddenOp;
pub const sensitive_patterns = descriptor.sensitive_patterns;
pub const bulletIsSensitive = descriptor.bulletIsSensitive;

pub const op_registry = table.op_registry;

pub const opsSection = prompt.opsSection;
pub const consoleBlock = prompt.consoleBlock;
pub const systemPrompt = prompt.systemPrompt;
pub const console_settings_prompt = prompt.console_settings_prompt;
pub const consoleSystemPrompt = prompt.consoleSystemPrompt;
pub const edge_map_suffix = prompt.edge_map_suffix;

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
    // Core §2 ops (incl. §2.1 image/save and the history ops) plus the cli-console profile — table order
    // IS prompt order, nothing more, nothing less, and never a forbidden name.
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

test {
    _ = descriptor;
    _ = table;
    _ = coreOps;
    _ = consoleOps;
    _ = prompt;
}
