//! §4 system-prompt assembly: the PROSE CORE is embedded verbatim (systemPrompt.json)
//! and the ops list is GENERATED from the registry table — the same table the validator's
//! variant gates read — so the prompt can never promise an op this console cannot run.
const std = @import("std");
const transport = @import("../transport.zig");
const descriptor = @import("descriptor.zig");
const table = @import("table.zig");

const OpCaps = descriptor.OpCaps;
const OpDescriptor = descriptor.OpDescriptor;
const full_capabilities = descriptor.full_capabilities;
const checkCensor = descriptor.checkCensor;
const op_registry = table.op_registry;

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
