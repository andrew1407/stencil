//! The op-plan's TYPES (contract §1–§3): the caps this client adds to the registry's,
//! the `Action` union every op normalizes into, and the `Plan`/`Ask` shapes a turn carries.
const std = @import("std");
const registry = @import("../registry.zig");
const opSchema = @import("../opSchema.zig");
const OpDescriptor = registry.OpDescriptor;
const op_registry = registry.op_registry;
const testing = std.testing;
const mustPlan = @import("validate.zig").mustPlan;

// The cli's own limits (the contract §1 caps live in the registry: opSchema.get().limits)

/// §7: how many images ONE message may carry — the `/upload`s a §2.1 `image` op indexes.
/// The working snapshot + its edge map ride on top and do not count against it.
pub const max_attachments = 8;
/// How long a sanitized variant label may get (mirrors the browser's 40-char cap).
pub const max_label_chars = 40;
/// Attachments larger than this are sent text-only with a note (contract-adjacent cap,
/// shared with mcp — the console cannot downscale without pulling a resampler into core).
pub const max_image_bytes: usize = 8 * 1024 * 1024;

// The op-plan (contract §1–§3)

pub const Dir = enum { left, right };
pub const FilterMode = enum { none, bw, sepia, invert, contour, custom };

/// The crop edges as the contract's cropSpec token strings (arena-owned). `aspect` is the strict
/// `W:H` token; `album` is the console-only §10 key (derive the missing axis from the page, landscape).
pub const CropEdges = struct {
    x1: ?[]const u8 = null,
    x2: ?[]const u8 = null,
    y1: ?[]const u8 = null,
    y2: ?[]const u8 = null,
    aspect: ?[]const u8 = null,
    album: bool = false,
};

/// One validated action (§2), already cleaned: `frame` normalizes index/indices to one list, `layout`
/// keeps its lines re-serialized as a JSON array string. All slices arena-owned by the Plan.
pub const Action = union(enum) {
    crop: CropEdges,
    rotate: struct { dir: Dir, times: u8 },
    filter: struct { mode: FilterMode, tint: []const u8 }, // tint "" unless custom
    layout: struct { lines_json: []const u8 }, // a JSON ARRAY of Line objects
    // §2 formula: axis+expr ("" = clear that axis), OR `enabled` alone (the on/off toggle
    // — axis is 0 and expr "" then).
    formula: struct { axis: u8, expr: []const u8, enabled: ?bool = null }, // axis 'x' | 'y'
    // §2 page: a lowercase ISO name, or "" with custom cm dims (exactly one form).
    page: struct { format: []const u8, width: f64 = 0, height: f64 = 0 },
    // §2 blank: optional cm dims (both or neither) override `format`.
    blank: struct { color: []const u8, format: ?[]const u8, width: f64 = 0, height: f64 = 0 },
    frame: struct { indices: []u32 },
    // §2 undo/redo/reset (top-level only): the surface's OWN edit history — the console's
    // /undo, /redo and /reset paths. One step = one history entry.
    undo: struct { steps: u8 },
    redo: struct { steps: u8 },
    reset,
    // §2.1 multi-image ops (top-level only): switch to the turn's Nth attachment / persist the current
    // image + layout. `name` is "" when the model left it out, and the executor derives one.
    image: struct { index: u32 },
    save: struct { name: []const u8, path: []const u8 },
    // Console-settings ops (the §10-analog profile) adjust the CONSOLE, not the image. `server`/`path`
    // are the model's raw strings — the executor resolves them, warnings rather than errors.
    accent: struct { color: []const u8, preset: []const u8 }, // exactly one non-empty (§10)
    connect: struct { server: []const u8 },
    disconnect: struct { server: []const u8 },
    reconnect: struct { server: []const u8 }, // the /reconnect path, connect's resolution
    delete: struct { path: []const u8 }, // a local .stencil path
    // §10 openUrl/copy, carried into the console profile: a URL load through the /upload
    // path (the user-echo guard runs at execution) and the /copy clipboard command.
    open_url: struct { url: []const u8, incognito: bool },
    // §10 openFile: a LOCAL path the user wrote (image/video, layout .json, or .stencil).
    // The echo guard runs at execution, exactly like openUrl's.
    open_file: struct { path: []const u8 },
    copy,
    // §10 clear, carried verbatim: drop the working image + lines (the console's /drop).
    clear,
    // §10 clearChat (top-level only): clear the conversation — deferred to the end of
    // the turn, confirmed in-app, then the /chat clear path.
    clear_chat,
};

/// The registry.zig descriptor carrying this wire name (its `Action` tag, bullet and
/// capability), or null for an op the console does not execute.
pub fn findOp(op: []const u8) ?*const OpDescriptor {
    for (&op_registry) |*d| {
        if (std.mem.eql(u8, op, d.name)) return d;
    }
    return null;
}

pub const Variant = struct {
    label: []const u8, // may be empty — file naming then falls back to the position
    actions: []Action,
};

/// One choice on an `ask` card (§11). A console cannot show a picture, so an option's preview is
/// dropped at parse time and only the label survives (§11.4); the option itself is NEVER dropped.
pub const AskOption = struct {
    label: []const u8,
};

/// A question put back to the user (contract §11), rendered as a numbered list and answered
/// by number on the next `/prompt`.
pub const Ask = struct {
    question: []const u8,
    multi: bool = false,
    allow_custom: bool = false,
    custom_label: []const u8, // always set from the registry by parseAsk
    options: []AskOption = &.{},
};

pub const Plan = struct {
    arena: std.heap.ArenaAllocator,
    reply: []const u8 = "",
    actions: []Action = &.{},
    variants: []Variant = &.{},
    ask: ?Ask = null,
    warnings: [][]const u8 = &.{}, // skipped-unknown-op notes, printed after the reply
    chat_only: bool = false, // no JSON object at all → raw text = reply, zero actions

    pub fn deinit(self: *Plan) void {
        self.arena.deinit();
    }

    /// True when any action (top-level or variant) is a `frame` op — the console's working
    /// input is never a video, so per the contract that is a plan-level error.
    pub fn hasFrameOp(self: *const Plan) bool {
        for (self.actions) |a| {
            if (a == .frame) return true;
        }
        for (self.variants) |v| for (v.actions) |a| {
            if (a == .frame) return true;
        };
        return false;
    }

    /// §7 auto-continuation: true when the plan LOADED a picture the model has not seen (`blank`/
    /// `openUrl`) and drew NO layout — the browser's planLoadsWithoutTracing.
    pub fn loadsWithoutTracing(self: *const Plan) bool {
        var loads = false;
        for (self.actions) |a| switch (a) {
            .blank, .open_url => loads = true, // console incognito still loads in place
            .layout => return false,
            else => {},
        };
        return loads;
    }
};

// registry.zig's table is NOT a copy of opRegistry.json: it adds the `Action` tag, the verbatim §4
// bullets and the capability. Only the op NAMES overlap, and the schema is parsed at runtime.
test "every op name has both a schema entry and an executor descriptor" {
    for (opSchema.get().entries) |e| try testing.expect(findOp(e.name) != null);
    for (op_registry) |d| try testing.expect(opSchema.get().find(d.name) != null);
}

test "loadsWithoutTracing: a load op continues the turn unless the plan drew a layout (§7)" {
    // Load mixed with pixel-free edits → continue; openUrl alone → continue.
    var mixed = try mustPlan("{\"reply\":\"x\",\"actions\":[{\"op\":\"blank\",\"color\":\"#ffffff\"},{\"op\":\"filter\",\"mode\":\"bw\"}]}");
    defer mixed.deinit();
    try testing.expect(mixed.loadsWithoutTracing());
    var open = try mustPlan("{\"reply\":\"x\",\"actions\":[{\"op\":\"openUrl\",\"url\":\"https://a.example/x.png\"}]}");
    defer open.deinit();
    try testing.expect(open.loadsWithoutTracing());
    // A plan that placed layout lines committed to its coordinates — no continuation.
    var traced = try mustPlan("{\"reply\":\"x\",\"actions\":[{\"op\":\"openUrl\",\"url\":\"https://a.example/x.png\"}," ++
        "{\"op\":\"layout\",\"lines\":[{\"points\":[{\"x\":0,\"y\":0}]}]}]}");
    defer traced.deinit();
    try testing.expect(!traced.loadsWithoutTracing());
    // No load op → nothing new to show the model.
    var edits = try mustPlan("{\"reply\":\"x\",\"actions\":[{\"op\":\"filter\",\"mode\":\"bw\"},{\"op\":\"copy\"}]}");
    defer edits.deinit();
    try testing.expect(!edits.loadsWithoutTracing());
}
