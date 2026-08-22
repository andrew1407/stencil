//! The op-plan (contract §1–§3) for the console LLM assistant: shared limits, the
//! validated plan model, the strict parser/validators, and the executor helpers
//! (crop specs, server resolution, console context, label sanitizing).
const std = @import("std");
const registry = @import("registry.zig");
const transport = @import("transport.zig");
const wire = @import("wire.zig");

// Symbols living in the sibling llm/ modules (facade: ../llm.zig).
const OpDescriptor = registry.OpDescriptor;
const isForbiddenOp = registry.isForbiddenOp;
const op_registry = registry.op_registry;
const Turn = wire.Turn;

// ── Limits (contract §1 — the same numbers in every client) ──────────────────

pub const max_actions = 16;
pub const max_variants = 8;

// §11 interactive replies — the same numbers as every other client.
pub const ask_min_options = 2;
pub const ask_max_options = 5;
pub const max_ask_question = 300;
pub const max_ask_label = 80;
pub const max_ask_answer = 500;
pub const default_custom_label = "Something else…";
pub const max_layout_lines = 200;
pub const max_frame_indices = 32;
pub const max_string_chars = 5000;
/// §2.1: the `save` op's optional project name.
pub const max_save_name = 120;
/// §10: the longest local path a `save`/`openFile` op may carry.
pub const max_path_chars = 1024;
/// §7: how many images ONE message may carry — the `/upload`s a §2.1 `image` op indexes.
/// The working snapshot + its edge map ride on top and do not count against it.
pub const max_attachments = 8;
/// How long a sanitized variant label may get (mirrors the browser's 40-char cap).
pub const max_label_chars = 40;
/// Attachments larger than this are sent text-only with a note (contract-adjacent cap,
/// shared with mcp — the console cannot downscale without pulling a resampler into core).
pub const max_image_bytes: usize = 8 * 1024 * 1024;

// ── The op-plan (contract §1–§3) ─────────────────────────────────────────────

pub const Dir = enum { left, right };
pub const FilterMode = enum { none, bw, sepia, invert, contour, custom };

/// The crop edges as the contract's cropSpec token strings (arena-owned). `aspect` is
/// the strict `W:H` token; `album` is the console-only §10 spec key (derive the missing
/// axis from the page, landscape — the `/crop … album` modifier).
pub const CropEdges = struct {
    x1: ?[]const u8 = null,
    x2: ?[]const u8 = null,
    y1: ?[]const u8 = null,
    y2: ?[]const u8 = null,
    aspect: ?[]const u8 = null,
    album: bool = false,
};

/// One validated action (§2), already cleaned: `frame` normalizes index/indices to one
/// list; `layout` keeps its validated lines re-serialized as a JSON array string (the
/// shape `/apply` consumes). All slices arena-owned by the Plan.
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
    // §2.1 multi-image ops (top-level only): switch to the turn's Nth attachment /
    // persist the current image + layout. `name` is "" when the model left it out —
    // the executor then derives one from the active attachment.
    image: struct { index: u32 },
    save: struct { name: []const u8, path: []const u8 },
    // Console-settings ops (the §10-analog profile above): they adjust the CONSOLE, not
    // the image. `server`/`path` are the model's raw strings — the executor resolves
    // them against the user's own connections / the /delete guards, warnings not errors.
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

/// The registry entry carrying this wire name, or null for an unknown op (§13: the
/// registry is the single source of an op's existence — flags come from here too).
pub fn findOp(op: []const u8) ?*const OpDescriptor {
    for (&op_registry) |*d| {
        if (std.mem.eql(u8, op, d.name)) return d;
    }
    return null;
}

/// §2/§2.1: ops allowed only in the TOP-LEVEL `actions` — inside a variant they cost it
/// its place: image/save switch/persist working images, undo/redo/reset step REAL edit
/// history, which a sandboxed variant render never has.
fn isTopLevelOnlyOp(op: []const u8) bool {
    const d = findOp(op) orelse return false;
    return d.top_level_only;
}

/// The console-settings ops: like §10's editor-settings ops they are forbidden inside
/// `variants` (variants exist to produce images) — a distinct message from §2.1's.
fn isConsoleSettingsOp(op: []const u8) bool {
    const d = findOp(op) orelse return false;
    return d.console_setting;
}

/// §1's one leniency: the first top-level-only or console-settings op in a raw variant
/// `actions` array (null = none). Such a variant is DROPPED with a warning instead of
/// failing the plan; malformed entries stay validateActions' business.
fn misplacedVariantOp(value: ?std.json.Value) ?[]const u8 {
    const v = value orelse return null;
    if (v != .array) return null;
    for (v.array.items) |raw| {
        if (raw != .object) continue;
        const op_v = raw.object.get("op") orelse continue;
        if (op_v != .string) continue;
        if (isTopLevelOnlyOp(op_v.string) or isConsoleSettingsOp(op_v.string)) return op_v.string;
    }
    return null;
}

pub const Variant = struct {
    label: []const u8, // may be empty — file naming then falls back to the position
    actions: []Action,
};

/// One choice on an `ask` card (§11). A console cannot show a picture, so an option's
/// preview is dropped at parse time and only the label survives (§11.4); the option
/// itself is NEVER dropped.
pub const AskOption = struct {
    label: []const u8,
};

/// A question put back to the user (contract §11), rendered as a numbered list and answered
/// by number on the next `/prompt`.
pub const Ask = struct {
    question: []const u8,
    multi: bool = false,
    allow_custom: bool = false,
    custom_label: []const u8 = default_custom_label,
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

    /// §7 auto-continuation: true when the plan LOADED a picture the model has not seen
    /// (`blank`/`openUrl` — `frame` is a plan error here) and drew NO layout: outlining
    /// needs pixels, and a plan that placed lines committed to them (the browser's
    /// planLoadsWithoutTracing).
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

/// Resolve what the user typed at an `ask` card (§11.3/§11.4: a console answers by
/// NUMBER — "2", or "1,3" when multi-select) into the joined labels, gpa-owned. Null =
/// not a selection, which is no error: the reply goes to the model as typed.
pub fn resolveAskAnswer(
    gpa: std.mem.Allocator,
    options: [][]u8,
    multi: bool,
    typed: []const u8,
) error{OutOfMemory}!?[]u8 {
    const t = std.mem.trim(u8, typed, " \t\r\n");
    if (t.len == 0 or options.len == 0) return null;

    var picked: std.ArrayList(usize) = .empty;
    defer picked.deinit(gpa);
    var it = std.mem.tokenizeAny(u8, t, ", \t");
    while (it.next()) |tok| {
        const n = std.fmt.parseInt(usize, tok, 10) catch return null; // not a selection → plain text
        if (n < 1 or n > options.len) return null; // a number nobody offered → plain text
        // A repeat is the user re-stating a pick, not a second one.
        if (std.mem.indexOfScalar(usize, picked.items, n - 1) == null) try picked.append(gpa, n - 1);
    }
    if (picked.items.len == 0) return null;
    if (picked.items.len > 1 and !multi) return null; // several numbers at a pick-one card

    var out: std.ArrayList(u8) = .empty;
    errdefer out.deinit(gpa);
    for (picked.items, 0..) |idx, i| {
        if (i != 0) try out.appendSlice(gpa, ", ");
        try out.appendSlice(gpa, options[idx]);
    }
    if (out.items.len > max_ask_answer) out.shrinkRetainingCapacity(max_ask_answer);
    return try out.toOwnedSlice(gpa);
}

/// parsePlan's outcome: a validated plan, or a user-facing rejection message (gpa-owned).
pub const ParseOutcome = union(enum) {
    plan: Plan,
    invalid: []u8,
};

/// Collects the first validation failure's user-facing message (gpa-owned).
const Diag = struct {
    gpa: std.mem.Allocator,
    msg: ?[]u8 = null,

    fn fail(self: *Diag, comptime fmt: []const u8, fargs: anytype) error{ Invalid, OutOfMemory } {
        if (self.msg == null) self.msg = try std.fmt.allocPrint(self.gpa, fmt, fargs);
        return error.Invalid;
    }

    fn deinit(self: *Diag) void {
        if (self.msg) |m| self.gpa.free(m);
        self.msg = null;
    }
};

/// Parse the raw LLM reply into a validated plan (§1): strip fences, take the first
/// balanced `{…}` object (none → chat-only turn); unknown ops drop with a warning, a
/// known op with bad params rejects the plan, a misplaced-op variant is dropped.
pub fn parsePlan(gpa: std.mem.Allocator, raw: []const u8) error{OutOfMemory}!ParseOutcome {
    var plan = Plan{ .arena = std.heap.ArenaAllocator.init(gpa) };
    errdefer plan.arena.deinit();
    const a = plan.arena.allocator();

    const stripped = try stripFences(a, raw);
    const object_src = firstJsonObject(stripped) orelse return chatOnly(&plan, a, raw);
    const root = std.json.parseFromSliceLeaky(std.json.Value, a, object_src, .{}) catch
        return chatOnly(&plan, a, raw); // braces that aren't actually JSON → chat-only
    if (root != .object) return chatOnly(&plan, a, raw);
    const obj = root.object;

    var diag = Diag{ .gpa = gpa };
    validateInto(&plan, a, obj, &diag) catch |e| switch (e) {
        error.Invalid => {
            plan.arena.deinit();
            return .{ .invalid = diag.msg.? };
        },
        error.OutOfMemory => {
            diag.deinit();
            return error.OutOfMemory;
        },
    };
    return .{ .plan = plan };
}

fn chatOnly(plan: *Plan, a: std.mem.Allocator, raw: []const u8) error{OutOfMemory}!ParseOutcome {
    plan.reply = try a.dupe(u8, std.mem.trim(u8, raw, " \t\r\n"));
    plan.chat_only = true;
    return .{ .plan = plan.* };
}

const ValidateError = error{ Invalid, OutOfMemory };

fn validateInto(plan: *Plan, a: std.mem.Allocator, obj: std.json.ObjectMap, diag: *Diag) ValidateError!void {
    // `version` other than 1 (or absent) is accepted but ignored (contract §1).
    var warnings: std.ArrayList([]const u8) = .empty;
    // §1 reply tolerance: models routinely omit the reply while planning valid
    // actions — substitute rather than lose the plan to a missing pleasantry.
    const reply_v = obj.get("reply");
    const reply_omitted = reply_v == null or reply_v.? != .string or
        std.mem.trim(u8, reply_v.?.string, " \t\r\n").len == 0;
    if (reply_omitted) {
        plan.reply = ""; // filled in below, once the plan's contents are known
    } else {
        plan.reply = try a.dupe(u8, reply_v.?.string);
    }
    plan.actions = try validateActions(a, obj.get("actions"), &warnings, "\"actions\"", diag);

    var variants: std.ArrayList(Variant) = .empty;
    if (obj.get("variants")) |vv| {
        if (vv != .null) {
            if (vv != .array) return diag.fail("invalid plan: \"variants\" must be an array", .{});
            if (vv.array.items.len > max_variants)
                return diag.fail("invalid plan: more than {d} variants", .{max_variants});
            for (vv.array.items, 0..) |raw_v, i| {
                if (raw_v != .object) return diag.fail("invalid plan: every variant must be an object", .{});
                const vo = raw_v.object;
                var label: []const u8 = "";
                if (nonNullField(vo, "label")) |lv| {
                    if (lv != .string or lv.string.len > max_string_chars)
                        return diag.fail("invalid plan: variant \"label\" must be a string", .{});
                    label = lv.string;
                }
                // §1: a variant carrying a top-level-only or console-settings op is
                // dropped with a warning naming it — the rest of the plan still runs.
                if (misplacedVariantOp(vo.get("actions"))) |op| {
                    try warnings.append(a, try droppedVariantWarning(a, i + 1, label, op));
                    continue;
                }
                // An absent/empty label stays empty — the executor's file naming falls
                // back to the variant's 1-based position ("variant-2.png").
                const owned_label = try a.dupe(u8, label);
                var where_buf: [24]u8 = undefined;
                const where = std.fmt.bufPrint(&where_buf, "variant {d}", .{i + 1}) catch "variant";
                const actions = try validateActions(a, vo.get("actions"), &warnings, where, diag);
                try variants.append(a, .{ .label = owned_label, .actions = actions });
            }
        }
    }
    plan.variants = try variants.toOwnedSlice(a);
    plan.ask = try validateAsk(a, obj.get("ask"), &warnings, diag);
    // The substitute must not overstate what happened: "Done." only when the
    // plan actually carries work — an empty plan says so, since a bare "Done."
    // there reads as a success that never occurred (contract §1).
    if (reply_omitted) {
        if (plan.actions.len > 0 or plan.variants.len > 0 or plan.ask != null) {
            plan.reply = try a.dupe(u8, "Done.");
            try warnings.append(a, try a.dupe(u8, "The model omitted its reply — the plan still ran"));
        } else {
            plan.reply = try a.dupe(u8, "The model returned an empty plan — nothing was changed.");
        }
    }
    plan.warnings = try warnings.toOwnedSlice(a);
}

/// The §1 note for a dropped variant: which one (its 1-based position, plus its label
/// when it has one) and why. Caller owns the result.
fn droppedVariantWarning(a: std.mem.Allocator, pos: usize, label: []const u8, op: []const u8) error{OutOfMemory}![]const u8 {
    const kind = if (isConsoleSettingsOp(op)) "console-settings op" else "top-level-only op";
    if (label.len == 0)
        return std.fmt.allocPrint(a, "Dropped variant {d}: the {s} \"{s}\" can't run inside a variant — the rest of the plan ran", .{ pos, kind, op });
    return std.fmt.allocPrint(a, "Dropped variant {d} (\"{s}\"): the {s} \"{s}\" can't run inside a variant — the rest of the plan ran", .{ pos, label, kind, op });
}

/// Validate the optional `ask` object (contract §11) → the card, or null when absent.
/// Strict, like an action: a card nobody can answer (no options, one option, six options,
/// an option that is both a render and a reference) rejects the whole plan.
fn validateAsk(
    a: std.mem.Allocator,
    value: ?std.json.Value,
    warnings: *std.ArrayList([]const u8),
    diag: *Diag,
) ValidateError!?Ask {
    const v = value orelse return null;
    if (v == .null) return null;
    if (v != .object) return diag.fail("invalid plan: \"ask\" must be an object", .{});
    const ao = v.object;

    var it = ao.iterator();
    while (it.next()) |kv| {
        const k = kv.key_ptr.*;
        if (!std.mem.eql(u8, k, "question") and !std.mem.eql(u8, k, "mode") and
            !std.mem.eql(u8, k, "options") and !std.mem.eql(u8, k, "allowCustom") and
            !std.mem.eql(u8, k, "customLabel"))
            return diag.fail("invalid plan: \"ask\" has unknown field \"{s}\"", .{k});
    }

    const q_v = ao.get("question");
    if (q_v == null or q_v.? != .string or std.mem.trim(u8, q_v.?.string, " \t\r\n").len == 0)
        return diag.fail("invalid plan: \"ask.question\" must be a non-empty string", .{});
    if (q_v.?.string.len > max_ask_question)
        return diag.fail("invalid plan: \"ask.question\" is longer than {d} characters", .{max_ask_question});

    var multi = false;
    if (nonNullField(ao, "mode")) |mv| {
        if (mv != .string) return diag.fail("invalid plan: \"ask.mode\" must be \"single\" or \"multi\"", .{});
        if (std.mem.eql(u8, mv.string, "multi")) {
            multi = true;
        } else if (!std.mem.eql(u8, mv.string, "single")) {
            return diag.fail("invalid plan: \"ask.mode\" must be \"single\" or \"multi\"", .{});
        }
    }

    var allow_custom = false;
    if (nonNullField(ao, "allowCustom")) |cv| {
        if (cv != .bool) return diag.fail("invalid plan: \"ask.allowCustom\" must be a boolean", .{});
        allow_custom = cv.bool;
    }

    var custom_label: []const u8 = default_custom_label;
    if (nonNullField(ao, "customLabel")) |lv| {
        if (lv != .string or std.mem.trim(u8, lv.string, " \t\r\n").len == 0)
            return diag.fail("invalid plan: \"ask.customLabel\" must be a non-empty string", .{});
        if (lv.string.len > max_ask_label)
            return diag.fail("invalid plan: \"ask.customLabel\" is longer than {d} characters", .{max_ask_label});
        custom_label = std.mem.trim(u8, lv.string, " \t\r\n");
    }

    const ov = ao.get("options");
    if (ov == null or ov.? != .array) return diag.fail("invalid plan: \"ask.options\" must be an array", .{});
    const items = ov.?.array.items;
    if (items.len < ask_min_options or items.len > ask_max_options)
        return diag.fail("invalid plan: \"ask.options\" must hold {d}..{d} options", .{ ask_min_options, ask_max_options });

    var opts: std.ArrayList(AskOption) = .empty;
    var dropped_preview = false;
    for (items, 0..) |raw_o, i| {
        if (raw_o != .object) return diag.fail("invalid plan: ask option {d} must be an object", .{i + 1});
        const oo = raw_o.object;
        var oit = oo.iterator();
        while (oit.next()) |kv| {
            const k = kv.key_ptr.*;
            if (!std.mem.eql(u8, k, "label") and !std.mem.eql(u8, k, "actions") and !std.mem.eql(u8, k, "image"))
                return diag.fail("invalid plan: ask option {d} has unknown field \"{s}\"", .{ i + 1, k });
        }
        const lv = oo.get("label");
        if (lv == null or lv.? != .string or std.mem.trim(u8, lv.?.string, " \t\r\n").len == 0)
            return diag.fail("invalid plan: ask option {d} \"label\" must be a non-empty string", .{i + 1});
        if (lv.?.string.len > max_ask_label)
            return diag.fail("invalid plan: ask option {d} \"label\" is longer than {d} characters", .{ i + 1, max_ask_label });
        if (nonNullField(oo, "actions") != null and nonNullField(oo, "image") != null)
            return diag.fail("invalid plan: ask option {d} carries both \"actions\" and \"image\"", .{i + 1});
        if (nonNullField(oo, "actions") != null or nonNullField(oo, "image") != null) dropped_preview = true;
        try opts.append(a, .{ .label = try a.dupe(u8, std.mem.trim(u8, lv.?.string, " \t\r\n")) });
    }
    // One note for the whole card, not one per option: a terminal cannot show any of them.
    if (dropped_preview)
        try warnings.append(a, try a.dupe(u8, "the console can't show option previews — the choices are listed by name"));

    return .{
        .question = try a.dupe(u8, std.mem.trim(u8, q_v.?.string, " \t\r\n")),
        .multi = multi,
        .allow_custom = allow_custom,
        .custom_label = try a.dupe(u8, custom_label),
        .options = try opts.toOwnedSlice(a),
    };
}

/// Validate one actions list: unknown ops drop with a warning (forward compatibility);
/// a known op with invalid params fails the whole plan. A variant's misplaced ops are
/// caught by `misplacedVariantOp` before this ever sees them (§1).
fn validateActions(
    a: std.mem.Allocator,
    value: ?std.json.Value,
    warnings: *std.ArrayList([]const u8),
    where: []const u8,
    diag: *Diag,
) ValidateError![]Action {
    const v = value orelse return &.{};
    if (v == .null) return &.{};
    if (v != .array) return diag.fail("invalid plan: {s} must be an array", .{where});
    const list = v.array.items;
    if (list.len > max_actions)
        return diag.fail("invalid plan: more than {d} actions in {s}", .{ max_actions, where });

    var out: std.ArrayList(Action) = .empty;
    for (list) |raw| {
        if (raw != .object)
            return diag.fail("invalid plan: every action in {s} must be an object with an \"op\"", .{where});
        const action = raw.object;
        const op_v = action.get("op");
        if (op_v == null or op_v.? != .string)
            return diag.fail("invalid plan: every action in {s} must be an object with an \"op\"", .{where});
        const op = op_v.?.string;
        const eqs = std.mem.eql;
        // §13: a forbidden ("never model-drivable") name is rejected outright — a hard
        // tooth distinct from the unknown-op skip, even though no such op is registered.
        if (isForbiddenOp(op))
            return diag.fail("invalid plan: the \"{s}\" op is never model-drivable", .{op});
        if (eqs(u8, op, "crop")) {
            try out.append(a, try validateCrop(a, action, diag));
        } else if (eqs(u8, op, "rotate")) {
            try out.append(a, try validateRotate(action, diag));
        } else if (eqs(u8, op, "filter")) {
            try out.append(a, try validateFilter(a, action, diag));
        } else if (eqs(u8, op, "layout")) {
            try out.append(a, try validateLayoutAction(a, action, diag));
        } else if (eqs(u8, op, "formula")) {
            try out.append(a, try validateFormulaAction(a, action, diag));
        } else if (eqs(u8, op, "page")) {
            try out.append(a, try validatePage(a, action, diag));
        } else if (eqs(u8, op, "blank")) {
            try out.append(a, try validateBlank(a, action, diag));
        } else if (eqs(u8, op, "frame")) {
            try out.append(a, try validateFrame(a, action, diag));
        } else if (eqs(u8, op, "image")) {
            try out.append(a, try validateImage(action, diag));
        } else if (eqs(u8, op, "save")) {
            try out.append(a, try validateSave(a, action, diag));
        } else if (eqs(u8, op, "undo")) {
            try out.append(a, .{ .undo = .{ .steps = try validateSteps(action, "undo", diag) } });
        } else if (eqs(u8, op, "redo")) {
            try out.append(a, .{ .redo = .{ .steps = try validateSteps(action, "redo", diag) } });
        } else if (eqs(u8, op, "reset")) {
            try onlyKeys(action, "reset", &.{}, diag); // no fields at all (§2)
            try out.append(a, .reset);
        } else if (eqs(u8, op, "accent")) {
            try out.append(a, try validateAccent(a, action, diag));
        } else if (eqs(u8, op, "connect")) {
            try out.append(a, .{ .connect = .{ .server = try validateServerField(a, action, "connect", diag) } });
        } else if (eqs(u8, op, "disconnect")) {
            try out.append(a, .{ .disconnect = .{ .server = try validateServerField(a, action, "disconnect", diag) } });
        } else if (eqs(u8, op, "reconnect")) {
            try out.append(a, .{ .reconnect = .{ .server = try validateServerField(a, action, "reconnect", diag) } });
        } else if (eqs(u8, op, "delete")) {
            try out.append(a, try validateDelete(a, action, diag));
        } else if (eqs(u8, op, "openFile")) {
            try out.append(a, try validateOpenFile(a, action, diag));
        } else if (eqs(u8, op, "openUrl")) {
            try out.append(a, try validateOpenUrl(a, action, diag));
        } else if (eqs(u8, op, "copy")) {
            try onlyKeys(action, "copy", &.{}, diag); // no fields at all (§10)
            try out.append(a, .copy);
        } else if (eqs(u8, op, "clear")) {
            try onlyKeys(action, "clear", &.{}, diag); // no fields at all (§10)
            try out.append(a, .clear);
        } else if (eqs(u8, op, "clearChat")) {
            try onlyKeys(action, "clearChat", &.{}, diag); // no fields at all (§10)
            try out.append(a, .clear_chat);
        } else {
            try warnings.append(a, try std.fmt.allocPrint(a, "Skipped unknown operation \"{s}\"", .{op}));
        }
    }
    return out.toOwnedSlice(a);
}

/// Reject unknown fields on a KNOWN op — that fails the whole plan (contract §1).
fn onlyKeys(action: std.json.ObjectMap, op: []const u8, allowed: []const []const u8, diag: *Diag) ValidateError!void {
    outer: for (action.keys()) |key| {
        if (std.mem.eql(u8, key, "op")) continue;
        for (allowed) |ok| {
            if (std.mem.eql(u8, key, ok)) continue :outer;
        }
        return diag.fail("invalid {s} action: unknown field \"{s}\"", .{ op, key });
    }
}

/// A string within the contract's per-field length limit, or null.
fn boundedStr(v: std.json.Value) ?[]const u8 {
    if (v != .string) return null;
    return if (v.string.len <= max_string_chars) v.string else null;
}

/// An action's field value, with an explicit JSON `null` treated as absent.
fn nonNullField(action: std.json.ObjectMap, key: []const u8) ?std.json.Value {
    const v = action.get(key) orelse return null;
    return if (v == .null) null else v;
}

/// A cropSpec token: optional `-`, a number (`12`, `1.5`, `.5`), optional `%`/`px`/`cm`/`in`.
fn isCropToken(token: []const u8) bool {
    const rest = if (std.mem.startsWith(u8, token, "-")) token[1..] else token;
    var i: usize = 0;
    while (i < rest.len and (std.ascii.isDigit(rest[i]) or rest[i] == '.')) : (i += 1) {}
    const number = rest[0..i];
    const unit = rest[i..];
    const valid_number = blk: {
        if (std.mem.indexOfScalar(u8, number, '.')) |dot| {
            const int = number[0..dot];
            const frac = number[dot + 1 ..];
            break :blk frac.len != 0 and allDigits(frac) and allDigits(int);
        }
        break :blk number.len != 0 and allDigits(number);
    };
    const valid_unit = unit.len == 0 or std.mem.eql(u8, unit, "%") or
        std.mem.eql(u8, unit, "px") or std.mem.eql(u8, unit, "cm") or std.mem.eql(u8, unit, "in");
    return valid_number and valid_unit;
}

fn allDigits(s: []const u8) bool {
    for (s) |c| {
        if (!std.ascii.isDigit(c)) return false;
    }
    return true;
}

/// A crop `aspect` token: strict `W:H`, digits only, both positive — the same rule
/// core/parse/cropSpec.cpp enforces at resolution time.
fn isAspectToken(token: []const u8) bool {
    const colon = std.mem.indexOfScalar(u8, token, ':') orelse return false;
    const w = token[0..colon];
    const h = token[colon + 1 ..];
    if (w.len == 0 or h.len == 0 or !allDigits(w) or !allDigits(h)) return false;
    return anyNonZeroDigit(w) and anyNonZeroDigit(h);
}

fn anyNonZeroDigit(s: []const u8) bool {
    for (s) |c| {
        if (c != '0') return true;
    }
    return false;
}

/// A lowercase ISO page name: `a0`–`a10`, `b0`–`b10`, `c0`–`c10` (contract §2 — lowercase).
fn isPageFormat(format: []const u8) bool {
    if (format.len < 2) return false;
    const series = format[0];
    if (series != 'a' and series != 'b' and series != 'c') return false;
    const number = format[1..];
    if (std.mem.eql(u8, number, "10")) return true;
    return number.len == 1 and std.ascii.isDigit(number[0]);
}

/// `#` + exactly 6 hex digits.
fn isHex6(color: []const u8) bool {
    if (color.len != 7 or color[0] != '#') return false;
    for (color[1..]) |c| {
        if (!std.ascii.isHex(c)) return false;
    }
    return true;
}

fn validateCrop(a: std.mem.Allocator, action: std.json.ObjectMap, diag: *Diag) ValidateError!Action {
    try onlyKeys(action, "crop", &.{ "spec", "aspect" }, diag);
    const spec_v = action.get("spec") orelse return diag.fail("invalid crop action: \"spec\" must be an object", .{});
    if (spec_v != .object) return diag.fail("invalid crop action: \"spec\" must be an object", .{});
    const spec = spec_v.object;
    for (spec.keys()) |key| {
        if (!std.mem.eql(u8, key, "x1") and !std.mem.eql(u8, key, "x2") and
            !std.mem.eql(u8, key, "y1") and !std.mem.eql(u8, key, "y2") and
            !std.mem.eql(u8, key, "aspect") and !std.mem.eql(u8, key, "album"))
            return diag.fail("invalid crop action: unknown spec key \"{s}\"", .{key});
    }
    var edges = CropEdges{};
    inline for (.{ "x1", "x2", "y1", "y2" }) |key| {
        if (spec.get(key)) |v| {
            const token = boundedStr(v) orelse return diag.fail("invalid crop action: bad token for \"{s}\"", .{key});
            if (!isCropToken(token)) return diag.fail("invalid crop action: bad token for \"{s}\"", .{key});
            @field(edges, key) = try a.dupe(u8, token);
        }
    }
    if (spec.get("aspect")) |v| {
        const token = boundedStr(v) orelse return diag.fail("invalid crop action: bad token for \"aspect\"", .{});
        if (!isAspectToken(token)) return diag.fail("invalid crop action: bad token for \"aspect\"", .{});
        edges.aspect = try a.dupe(u8, token);
    }
    // The console-only "album" spec key (§10): a bool riding beside the edge tokens,
    // threaded to the same axis derivation the `/crop … album` modifier uses.
    if (nonNullField(spec, "album")) |v| {
        if (v != .bool) return diag.fail("invalid crop action: \"album\" must be a boolean", .{});
        edges.album = v.bool;
    }
    // Action-level aspect tolerance: models sometimes put "aspect" beside "spec" —
    // fold it in when the spec lacks it; a conflicting duplicate fails the plan.
    if (nonNullField(action, "aspect")) |v| {
        const token = boundedStr(v) orelse return diag.fail("invalid crop action: bad token for \"aspect\"", .{});
        if (!isAspectToken(token)) return diag.fail("invalid crop action: bad token for \"aspect\"", .{});
        if (edges.aspect) |inside| {
            if (!std.mem.eql(u8, inside, token))
                return diag.fail("invalid crop action: \"aspect\" appears both beside \"spec\" and inside it with different values", .{});
        } else edges.aspect = try a.dupe(u8, token);
    }
    if (edges.x1 == null and edges.x2 == null and edges.y1 == null and edges.y2 == null and edges.aspect == null)
        return diag.fail("invalid crop action: spec needs at least one of x1/x2/y1/y2/aspect", .{});
    return .{ .crop = edges };
}

fn validateRotate(action: std.json.ObjectMap, diag: *Diag) ValidateError!Action {
    try onlyKeys(action, "rotate", &.{ "dir", "times" }, diag);
    const dir = enumField(Dir, action, "dir") orelse
        return diag.fail("invalid rotate action: \"dir\" must be \"left\" or \"right\"", .{});
    var times: u8 = 1;
    if (nonNullField(action, "times")) |tv| {
        if (tv != .integer or tv.integer < 1 or tv.integer > 3)
            return diag.fail("invalid rotate action: \"times\" must be an integer 1..3", .{});
        times = @intCast(tv.integer);
    }
    return .{ .rotate = .{ .dir = dir, .times = times } };
}

/// An action's string field parsed as an enum literal (exact tag names), or null.
fn enumField(comptime E: type, action: std.json.ObjectMap, key: []const u8) ?E {
    const v = action.get(key) orelse return null;
    if (v != .string) return null;
    return std.meta.stringToEnum(E, v.string);
}

fn validateFilter(a: std.mem.Allocator, action: std.json.ObjectMap, diag: *Diag) ValidateError!Action {
    try onlyKeys(action, "filter", &.{ "mode", "tint" }, diag);
    const mode = enumField(FilterMode, action, "mode") orelse
        return diag.fail("invalid filter action: unknown \"mode\"", .{});
    const tint_v = nonNullField(action, "tint");
    if (mode == .custom) {
        const tint = if (tint_v) |t| boundedStr(t) else null;
        if (tint == null or !isHex6(tint.?))
            return diag.fail("invalid filter action: \"custom\" requires \"tint\" as #rrggbb", .{});
        return .{ .filter = .{ .mode = mode, .tint = try a.dupe(u8, tint.?) } };
    }
    if (tint_v != null) return diag.fail("invalid filter action: \"tint\" is only valid with mode \"custom\"", .{});
    return .{ .filter = .{ .mode = mode, .tint = "" } };
}

fn validateLayoutAction(a: std.mem.Allocator, action: std.json.ObjectMap, diag: *Diag) ValidateError!Action {
    try onlyKeys(action, "layout", &.{"lines"}, diag);
    const lines_v = action.get("lines") orelse return diag.fail("invalid layout action: \"lines\" must be an array", .{});
    if (lines_v != .array) return diag.fail("invalid layout action: \"lines\" must be an array", .{});
    const lines = lines_v.array.items;
    if (lines.len > max_layout_lines)
        return diag.fail("invalid layout action: more than {d} lines", .{max_layout_lines});
    for (lines) |raw| try validateLine(raw, diag);
    // Every field validated + unknown keys rejected, so the raw array re-serializes into
    // exactly the layout `lines` document the session's /apply path already consumes.
    const json = std.json.Stringify.valueAlloc(a, lines_v, .{}) catch return error.OutOfMemory;
    return .{ .layout = .{ .lines_json = json } };
}

/// Validate one layout Line per contract §3 (the shared browser line schema; per-line
/// defaults apply for omitted fields, so only present fields are checked).
fn validateLine(raw: std.json.Value, diag: *Diag) ValidateError!void {
    if (raw != .object) return diag.fail("invalid layout action: each line must be an object", .{});
    const line = raw.object;
    const line_keys = [_][]const u8{ "points", "color", "thickness", "pointSize", "style", "locked", "fillColor" };
    outer: for (line.keys()) |key| {
        for (line_keys) |ok| {
            if (std.mem.eql(u8, key, ok)) continue :outer;
        }
        return diag.fail("invalid layout action: unknown line field \"{s}\"", .{key});
    }
    const points_v = line.get("points") orelse return diag.fail("invalid layout action: line \"points\" must be an array", .{});
    if (points_v != .array) return diag.fail("invalid layout action: line \"points\" must be an array", .{});
    for (points_v.array.items) |pt| {
        if (pt != .object) return diag.fail("invalid layout action: points must be {{x, y}} objects", .{});
        const p = pt.object;
        for (p.keys()) |key| {
            if (!std.mem.eql(u8, key, "x") and !std.mem.eql(u8, key, "y"))
                return diag.fail("invalid layout action: points must be {{x, y}} objects", .{});
        }
        if (finiteNum(p.get("x")) == null or finiteNum(p.get("y")) == null)
            return diag.fail("invalid layout action: point coords must be finite numbers", .{});
    }
    inline for (.{ "color", "fillColor" }) |key| {
        if (nonNullField(line, key)) |v| {
            if (boundedStr(v) == null)
                return diag.fail("invalid layout action: line \"{s}\" must be a string", .{key});
        }
    }
    inline for (.{ "thickness", "pointSize" }) |key| {
        if (nonNullField(line, key)) |v| {
            if (finiteNum(v) == null)
                return diag.fail("invalid layout action: line \"{s}\" must be a number", .{key});
        }
    }
    if (nonNullField(line, "style")) |v| {
        if (v != .string or std.meta.stringToEnum(enum { solid, dashed, dotted }, v.string) == null)
            return diag.fail("invalid layout action: unknown line style", .{});
    }
    if (nonNullField(line, "locked")) |v| {
        if (v != .bool)
            return diag.fail("invalid layout action: line \"locked\" must be a boolean", .{});
    }
}

fn finiteNum(v: ?std.json.Value) ?f64 {
    const val = v orelse return null;
    return switch (val) {
        .integer => |i| @floatFromInt(i),
        .float => |f| if (std.math.isFinite(f)) f else null,
        else => null,
    };
}

fn validateFormulaAction(a: std.mem.Allocator, action: std.json.ObjectMap, diag: *Diag) ValidateError!Action {
    try onlyKeys(action, "formula", &.{ "axis", "expr", "enabled" }, diag);
    // §2: `enabled` rides ALONE — the formulas on/off toggle (false restores identity).
    if (nonNullField(action, "enabled")) |ev| {
        if (nonNullField(action, "axis") != null or nonNullField(action, "expr") != null)
            return diag.fail("invalid formula action: \"enabled\" rides alone, without \"axis\"/\"expr\"", .{});
        if (ev != .bool) return diag.fail("invalid formula action: \"enabled\" must be a boolean", .{});
        return .{ .formula = .{ .axis = 0, .expr = "", .enabled = ev.bool } };
    }
    const axis_e = enumField(enum { x, y }, action, "axis") orelse
        return diag.fail("invalid formula action: \"axis\" must be \"x\" or \"y\"", .{});
    const axis: u8 = @tagName(axis_e)[0];
    const expr_v = action.get("expr") orelse return diag.fail("invalid formula action: \"expr\" must be a string", .{});
    const expr = boundedStr(expr_v) orelse return diag.fail("invalid formula action: \"expr\" must be a string", .{});
    // An empty (or blank) expression clears that axis (§2).
    if (std.mem.trim(u8, expr, " \t").len == 0)
        return .{ .formula = .{ .axis = axis, .expr = "" } };
    // Charset check with the single variable matching the axis; the shared core formula
    // engine validates the expression again before use (contract §2).
    for (expr) |c| {
        const ok = std.ascii.isDigit(c) or c == axis or
            std.mem.indexOfScalar(u8, "+-*/(). ", c) != null;
        if (!ok)
            return diag.fail("invalid formula action: \"expr\" may only use digits, + - * / ( ) . and \"{c}\"", .{axis});
    }
    return .{ .formula = .{ .axis = axis, .expr = try a.dupe(u8, expr) } };
}

/// A custom-page/blank cm dimension (§2): a finite number in 0.1..500, or null.
fn cmDim(v: std.json.Value) ?f64 {
    const n = finiteNum(v) orelse return null;
    return if (n >= 0.1 and n <= 500) n else null;
}

fn validatePage(a: std.mem.Allocator, action: std.json.ObjectMap, diag: *Diag) ValidateError!Action {
    try onlyKeys(action, "page", &.{ "format", "width", "height" }, diag);
    const w_v = nonNullField(action, "width");
    const h_v = nonNullField(action, "height");
    // §2: `format` OR custom cm dims — exactly one of the two forms.
    if (nonNullField(action, "format") != null and (w_v != null or h_v != null))
        return diag.fail("invalid page action: exactly one of \"format\" / \"width\"+\"height\" is required", .{});
    if (w_v != null or h_v != null) {
        if (w_v == null or h_v == null)
            return diag.fail("invalid page action: custom dims need both \"width\" and \"height\"", .{});
        const w = cmDim(w_v.?) orelse
            return diag.fail("invalid page action: \"width\"/\"height\" must be centimetres 0.1..500", .{});
        const h = cmDim(h_v.?) orelse
            return diag.fail("invalid page action: \"width\"/\"height\" must be centimetres 0.1..500", .{});
        return .{ .page = .{ .format = "", .width = w, .height = h } };
    }
    if (action.get("format")) |v| {
        if (boundedStr(v)) |format| {
            if (isPageFormat(format)) return .{ .page = .{ .format = try a.dupe(u8, format) } };
        }
    }
    return diag.fail("invalid page action: \"format\" must be a lowercase ISO name a0\u{2013}a10, b0\u{2013}b10 or c0\u{2013}c10", .{});
}

fn validateBlank(a: std.mem.Allocator, action: std.json.ObjectMap, diag: *Diag) ValidateError!Action {
    try onlyKeys(action, "blank", &.{ "color", "format", "width", "height" }, diag);
    const color = blk: {
        if (action.get("color")) |v| {
            if (boundedStr(v)) |color| {
                if (isHex6(color) or (color.len != 0 and allAlpha(color))) break :blk color;
            }
        }
        return diag.fail("invalid blank action: \"color\" must be #rrggbb or a CSS color name", .{});
    };
    var format: ?[]const u8 = null;
    if (nonNullField(action, "format")) |v| {
        const f = boundedStr(v) orelse return diag.fail("invalid blank action: bad page \"format\"", .{});
        if (!isPageFormat(f)) return diag.fail("invalid blank action: bad page \"format\"", .{});
        format = try a.dupe(u8, f);
    }
    // §2: explicit cm dims — both or neither; at execution they override `format`.
    var width: f64 = 0;
    var height: f64 = 0;
    const w_v = nonNullField(action, "width");
    const h_v = nonNullField(action, "height");
    if ((w_v == null) != (h_v == null))
        return diag.fail("invalid blank action: \"width\" and \"height\" ride together (both or neither)", .{});
    if (w_v != null) {
        width = cmDim(w_v.?) orelse
            return diag.fail("invalid blank action: \"width\"/\"height\" must be centimetres 0.1..500", .{});
        height = cmDim(h_v.?) orelse
            return diag.fail("invalid blank action: \"width\"/\"height\" must be centimetres 0.1..500", .{});
    }
    return .{ .blank = .{ .color = try a.dupe(u8, color), .format = format, .width = width, .height = height } };
}

fn allAlpha(s: []const u8) bool {
    for (s) |c| {
        if (!std.ascii.isAlphabetic(c)) return false;
    }
    return true;
}

fn validateFrame(a: std.mem.Allocator, action: std.json.ObjectMap, diag: *Diag) ValidateError!Action {
    try onlyKeys(action, "frame", &.{ "index", "indices" }, diag);
    const index_v = nonNullField(action, "index");
    const indices_v = nonNullField(action, "indices");
    if ((index_v == null) == (indices_v == null))
        return diag.fail("invalid frame action: exactly one of \"index\" / \"indices\" is required", .{});
    if (index_v) |v| {
        const idx = frameIndex(v) orelse return diag.fail("invalid frame action: \"index\" must be an integer >= 0", .{});
        const one = try a.alloc(u32, 1);
        one[0] = idx;
        return .{ .frame = .{ .indices = one } };
    }
    const list_v = indices_v.?;
    if (list_v != .array or list_v.array.items.len == 0)
        return diag.fail("invalid frame action: \"indices\" must be a non-empty array", .{});
    const items = list_v.array.items;
    if (items.len > max_frame_indices)
        return diag.fail("invalid frame action: more than {d} indices", .{max_frame_indices});
    const out = try a.alloc(u32, items.len);
    for (items, 0..) |item, i| {
        out[i] = frameIndex(item) orelse return diag.fail("invalid frame action: indices must be integers >= 0", .{});
    }
    return .{ .frame = .{ .indices = out } };
}

/// §2.1 `image`: the 1-based index of the turn attachment to make the working image.
fn validateImage(action: std.json.ObjectMap, diag: *Diag) ValidateError!Action {
    try onlyKeys(action, "image", &.{"index"}, diag);
    const v = nonNullField(action, "index") orelse
        return diag.fail("invalid image action: \"index\" must be an integer >= 1", .{});
    const idx = frameIndex(v) orelse
        return diag.fail("invalid image action: \"index\" must be an integer >= 1", .{});
    if (idx < 1) return diag.fail("invalid image action: \"index\" must be an integer >= 1", .{});
    return .{ .image = .{ .index = idx } };
}

/// §2.1 `save`: an optional project name (≤ 120 chars); "" = derive one at execution.
fn validateSave(a: std.mem.Allocator, action: std.json.ObjectMap, diag: *Diag) ValidateError!Action {
    try onlyKeys(action, "save", &.{ "name", "path" }, diag);
    var name: []const u8 = "";
    if (nonNullField(action, "name")) |v| {
        if (v != .string or v.string.len > max_save_name)
            return diag.fail("invalid save action: \"name\" must be a string of at most {d} characters", .{max_save_name});
        name = v.string;
    }
    var path: []const u8 = "";
    if (nonNullField(action, "path")) |v| {
        if (v != .string or v.string.len > max_path_chars)
            return diag.fail("invalid save action: \"path\" must be a string of at most {d} characters", .{max_path_chars});
        path = std.mem.trim(u8, v.string, " \t");
        if (looksRemote(path)) return diag.fail("invalid save action: \"path\" is a local path, not a URL", .{});
    }
    const dup_name = try a.dupe(u8, name);
    return .{ .save = .{ .name = dup_name, .path = try a.dupe(u8, path) } };
}

/// §10 `openFile`: a local path with an extension this app understands (an image/video, a
/// layout `.json`, or a `.stencil` project). Whether the USER actually wrote it is checked at
/// execution, like openUrl's guard.
fn validateOpenFile(a: std.mem.Allocator, action: std.json.ObjectMap, diag: *Diag) ValidateError!Action {
    try onlyKeys(action, "openFile", &.{"path"}, diag);
    if (nonNullField(action, "path")) |v| {
        if (boundedStr(v)) |raw| {
            const path = std.mem.trim(u8, raw, " \t");
            if (path.len != 0 and path.len <= max_path_chars and !looksRemote(path)) {
                if (!understoodPath(path)) {
                    return diag.fail(
                        "invalid openFile action: \"{s}\" is not an image, video, .json layout or .stencil project",
                        .{path},
                    );
                }
                return .{ .open_file = .{ .path = try a.dupe(u8, path) } };
            }
        }
    }
    return diag.fail("invalid openFile action: \"path\" must be a local file path", .{});
}

/// True for anything with a URL scheme — a local-path field must never carry one (openUrl is
/// the op for those, and it has its own network guard).
fn looksRemote(path: []const u8) bool {
    const colon = std.mem.indexOfScalar(u8, path, ':') orelse return false;
    if (colon == 0 or colon + 2 >= path.len) return false;
    return path[colon + 1] == '/' and path[colon + 2] == '/';
}

/// The read scope: only the formats the app itself opens. Extension-based on purpose — the
/// model never gets to hand us an arbitrary file to slurp.
pub fn understoodPath(path: []const u8) bool {
    const dot = std.mem.lastIndexOfScalar(u8, path, '.') orelse return false;
    if (std.mem.lastIndexOfAny(u8, path, "/\\")) |slash| {
        if (dot < slash) return false; // the dot is in a directory name
    }
    const ext = path[dot + 1 ..];
    for (understood_exts) |known| {
        if (std.ascii.eqlIgnoreCase(ext, known)) return true;
    }
    return false;
}

/// Image, video, layout and project extensions — the CLI's own input set.
const understood_exts = [_][]const u8{
    "png",  "jpg", "jpeg", "bmp", "tga", "gif", "webp",
    "mp4",  "mov", "m4v",  "avi", "mkv", "webm",
    "json", "stencil",
};

/// §2 undo/redo: the optional `steps` count, an integer 1..20 (default 1).
fn validateSteps(action: std.json.ObjectMap, op: []const u8, diag: *Diag) ValidateError!u8 {
    try onlyKeys(action, op, &.{"steps"}, diag);
    if (nonNullField(action, "steps")) |v| {
        if (v != .integer or v.integer < 1 or v.integer > 20)
            return diag.fail("invalid {s} action: \"steps\" must be an integer 1..20", .{op});
        return @intCast(v.integer);
    }
    return 1;
}

/// Console-settings `accent`: a strict #rrggbb `color` (§10 — the model translates
/// colour names itself) OR a named `preset`, exactly one form; unknown presets note +
/// skip at execution.
fn validateAccent(a: std.mem.Allocator, action: std.json.ObjectMap, diag: *Diag) ValidateError!Action {
    try onlyKeys(action, "accent", &.{ "color", "preset" }, diag);
    const color_v = nonNullField(action, "color");
    const preset_v = nonNullField(action, "preset");
    if ((color_v == null) == (preset_v == null))
        return diag.fail("invalid accent action: exactly one of \"color\" / \"preset\" is required", .{});
    if (preset_v) |v| {
        if (boundedStr(v)) |preset| {
            const trimmed = std.mem.trim(u8, preset, " \t");
            if (trimmed.len != 0)
                return .{ .accent = .{ .color = "", .preset = try a.dupe(u8, trimmed) } };
        }
        return diag.fail("invalid accent action: \"preset\" must be a non-empty string", .{});
    }
    if (boundedStr(color_v.?)) |color| {
        if (isHex6(color)) return .{ .accent = .{ .color = try a.dupe(u8, color), .preset = "" } };
    }
    return diag.fail("invalid accent action: \"color\" must be #rrggbb", .{});
}

/// The shared connect/disconnect field: a non-empty `server` string. Resolution against
/// the user's own servers happens at execution — validation only checks the shape.
fn validateServerField(a: std.mem.Allocator, action: std.json.ObjectMap, op: []const u8, diag: *Diag) ValidateError![]const u8 {
    try onlyKeys(action, op, &.{"server"}, diag);
    if (nonNullField(action, "server")) |v| {
        if (boundedStr(v)) |s| {
            const trimmed = std.mem.trim(u8, s, " \t");
            if (trimmed.len != 0) return try a.dupe(u8, trimmed);
        }
    }
    return diag.fail("invalid {s} action: \"server\" must be a non-empty string", .{op});
}

/// §10 `openUrl`: an http(s) URL + optional `incognito` bool. Whether the USER actually
/// wrote the URL is the plan-level guard's job (urlEchoedByUser) — this checks shape only.
fn validateOpenUrl(a: std.mem.Allocator, action: std.json.ObjectMap, diag: *Diag) ValidateError!Action {
    try onlyKeys(action, "openUrl", &.{ "url", "incognito" }, diag);
    var incognito = false;
    if (nonNullField(action, "incognito")) |v| {
        if (v != .bool) return diag.fail("invalid openUrl action: \"incognito\" must be a boolean", .{});
        incognito = v.bool;
    }
    if (nonNullField(action, "url")) |v| {
        if (boundedStr(v)) |s| {
            const url = std.mem.trim(u8, s, " \t\r\n");
            if (isHttpUrl(url))
                return .{ .open_url = .{ .url = try a.dupe(u8, url), .incognito = incognito } };
        }
    }
    return diag.fail("invalid openUrl action: \"url\" must be an http(s) URL", .{});
}

/// `http(s)://` + at least one character, none of them whitespace — the browser
/// validator's `/^https?:\/\/\S+$/i`.
fn isHttpUrl(url: []const u8) bool {
    const scheme_len: usize = if (std.ascii.startsWithIgnoreCase(url, "https://"))
        "https://".len
    else if (std.ascii.startsWithIgnoreCase(url, "http://"))
        "http://".len
    else
        return false;
    if (url.len == scheme_len) return false;
    for (url) |c| {
        if (std.ascii.isWhitespace(c)) return false;
    }
    return true;
}

/// §10 openUrl guard: the model may only ECHO the user — true when `url` appears
/// verbatim in the current turn's text or a replayed USER turn (assistant text and
/// fetched/attached content never count).
pub fn urlEchoedByUser(history: []const Turn, current_text: []const u8, url: []const u8) bool {
    if (std.mem.indexOf(u8, current_text, url) != null) return true;
    for (history) |t| {
        if (t.role == .user and std.mem.indexOf(u8, t.text, url) != null) return true;
    }
    return false;
}

/// The same guard for a LOCAL path (`openFile`, a `save` destination): the model may only
/// touch a place the user named — the path itself or a FOLDER it sits in ("save it to
/// ~/Downloads"); a `..` anywhere voids the grant.
pub fn pathEchoedByUser(history: []const Turn, current_text: []const u8, path: []const u8) bool {
    if (urlEchoedByUser(history, current_text, path)) return true;
    if (std.mem.indexOf(u8, path, "..") != null) return false;
    var end = path.len;
    while (std.mem.lastIndexOfScalar(u8, path[0..end], '/')) |slash| {
        if (slash == 0) return false; // "/" alone grants nothing
        if (folderNamedByUser(history, current_text, path[0..slash])) return true;
        end = slash;
    }
    return false;
}

/// True when the user wrote `dir` as a path of its OWN — not merely as the head of a longer
/// one. Without that distinction, naming a single file ("open ~/Pictures/cat.png") would hand
/// the model the whole folder it sits in.
fn folderNamedByUser(history: []const Turn, current_text: []const u8, dir: []const u8) bool {
    if (namedAsPathIn(current_text, dir)) return true;
    for (history) |t| {
        if (t.role == .user and namedAsPathIn(t.text, dir)) return true;
    }
    return false;
}

/// `needle` appears in `text` as a complete path token: at the end, or followed by whitespace
/// or sentence punctuation — never by another path segment.
fn namedAsPathIn(text: []const u8, needle: []const u8) bool {
    var i: usize = 0;
    while (std.mem.indexOfPos(u8, text, i, needle)) |at| : (i = at + 1) {
        const after = at + needle.len;
        if (after >= text.len) return true;
        const c = text[after];
        if (std.ascii.isWhitespace(c)) return true;
        switch (c) {
            ',', ';', ':', '"', '\'', ')', ']', '}', '!', '?' => return true,
            // A dot only ends the token when it ends the sentence, so "~/Downloads.png"
            // (a file the user named) never grants the "~/Downloads" folder.
            '.' => if (after + 1 >= text.len or std.ascii.isWhitespace(text[after + 1])) return true,
            else => {},
        }
    }
    return false;
}

/// Console-settings `delete`: a non-empty path. The executor applies the SAME guards the
/// /delete command uses (.stencil only, no URLs, no escaping the working directory).
fn validateDelete(a: std.mem.Allocator, action: std.json.ObjectMap, diag: *Diag) ValidateError!Action {
    try onlyKeys(action, "delete", &.{"path"}, diag);
    if (nonNullField(action, "path")) |v| {
        if (boundedStr(v)) |p| {
            const trimmed = std.mem.trim(u8, p, " \t");
            if (trimmed.len != 0) return .{ .delete = .{ .path = try a.dupe(u8, trimmed) } };
        }
    }
    return diag.fail("invalid delete action: \"path\" must be a non-empty string", .{});
}

fn frameIndex(v: std.json.Value) ?u32 {
    if (v != .integer) return null;
    if (v.integer < 0 or v.integer > std.math.maxInt(u32)) return null;
    return @intCast(v.integer);
}

// ── Extraction helpers (fences + the first balanced object) ──────────────────

/// Remove Markdown code fences (``` with an optional language tag), keeping the rest of
/// the text intact — the JS reference's `raw.replace(/```[a-zA-Z]*/g, '')`.
fn stripFences(a: std.mem.Allocator, text: []const u8) error{OutOfMemory}![]u8 {
    var out: std.ArrayList(u8) = .empty;
    var rest = text;
    while (std.mem.indexOf(u8, rest, "```")) |i| {
        try out.appendSlice(a, rest[0..i]);
        rest = rest[i + 3 ..];
        var n: usize = 0;
        while (n < rest.len and std.ascii.isAlphabetic(rest[n])) : (n += 1) {}
        rest = rest[n..];
    }
    try out.appendSlice(a, rest);
    return out.toOwnedSlice(a);
}

/// The first balanced `{ … }` slice (string- and escape-aware), or null.
fn firstJsonObject(text: []const u8) ?[]const u8 {
    const start = std.mem.indexOfScalar(u8, text, '{') orelse return null;
    var depth: usize = 0;
    var in_string = false;
    var escaped = false;
    for (text[start..], start..) |c, i| {
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        switch (c) {
            '"' => in_string = true,
            '{' => depth += 1,
            '}' => {
                depth -= 1;
                if (depth == 0) return text[start .. i + 1];
            },
            else => {},
        }
    }
    return null;
}

// ── Executor helpers ─────────────────────────────────────────────────────────

/// Join the crop edges back into the CLI's crop-spec grammar (`x1=10% x2=-10%`), the same
/// string `/crop` takes. Caller owns the result.
pub fn cropSpecString(gpa: std.mem.Allocator, edges: CropEdges) error{OutOfMemory}![]u8 {
    var out: std.ArrayList(u8) = .empty;
    errdefer out.deinit(gpa);
    inline for (.{ "x1", "x2", "y1", "y2", "aspect" }) |key| {
        if (@field(edges, key)) |token| {
            if (out.items.len != 0) try out.append(gpa, ' ');
            try out.appendSlice(gpa, key);
            try out.append(gpa, '=');
            try out.appendSlice(gpa, token);
        }
    }
    return out.toOwnedSlice(gpa);
}

/// resolveServer's outcome: the matched entry's index, or why nothing matched (the
/// caller's warning — never a plan failure on the console).
pub const ServerMatch = union(enum) { index: usize, none, ambiguous };

/// §10's connect/disconnect stance, over the console's own URL lists: exact URL match,
/// else a UNIQUE host (or host:port) match, case-insensitive on the host. The model can
/// never introduce a new address — an unmatched name is the user's to /connect.
pub fn resolveServer(urls: []const []const u8, want_raw: []const u8) ServerMatch {
    const want = std.mem.trim(u8, want_raw, " \t");
    if (want.len == 0) return .none;
    for (urls, 0..) |u, i| {
        if (std.mem.eql(u8, u, want)) return .{ .index = i };
    }
    var found: ?usize = null;
    for (urls, 0..) |u, i| {
        const auth = urlAuthority(u);
        if (std.ascii.eqlIgnoreCase(auth, want) or std.ascii.eqlIgnoreCase(urlHostname(auth), want)) {
            if (found != null) return .ambiguous;
            found = i;
        }
    }
    return if (found) |i| .{ .index = i } else .none;
}

/// "scheme://host:port/path" → "host:port" (userinfo-free server bases only).
fn urlAuthority(url: []const u8) []const u8 {
    var rest = url;
    if (std.mem.indexOf(u8, rest, "://")) |i| rest = rest[i + 3 ..];
    const end = std.mem.indexOfScalar(u8, rest, '/') orelse rest.len;
    return rest[0..end];
}

/// "host:port" → "host" (a bracketed IPv6 literal keeps its brackets).
fn urlHostname(auth: []const u8) []const u8 {
    if (std.mem.startsWith(u8, auth, "[")) {
        if (std.mem.indexOfScalar(u8, auth, ']')) |i| return auth[0 .. i + 1];
        return auth;
    }
    if (std.mem.lastIndexOfScalar(u8, auth, ':')) |i| return auth[0..i];
    return auth;
}

/// One live connection as the console-context suffix sees it: the URL and (optionally)
/// its project names — NEVER a token; this struct has nowhere to put one.
pub const ConsoleServer = struct {
    url: []const u8,
    active: bool = false, // hosts the active fetched project
    projects: ?[]const []const u8 = null, // null = not fetched/unreachable (line omitted)
};

/// How many project names one server contributes to the context suffix.
pub const max_context_projects = 20;

/// The console's dynamic system-prompt suffix (§4 allows one): the connection list (URLs
/// only), the active project, and each server's project names, so connect/disconnect ops
/// resolve against addresses the user already owns. Caller owns the result.
pub fn consoleContextAlloc(
    gpa: std.mem.Allocator,
    servers: []const ConsoleServer,
    active_project: []const u8,
) error{OutOfMemory}![]u8 {
    var out: std.ArrayList(u8) = .empty;
    errdefer out.deinit(gpa);
    const w = "Console state (the console's own connections and project, for answering questions about it):\n";
    try out.appendSlice(gpa, w);
    if (servers.len == 0) {
        try out.appendSlice(gpa, "Connections: none — the user can add one with '/connect <url>'.");
    } else {
        try appendFmt(gpa, &out, "Connections ({d}): ", .{servers.len});
        for (servers, 0..) |s, i| {
            if (i != 0) try out.appendSlice(gpa, ", ");
            try out.appendSlice(gpa, s.url);
            if (s.active) try out.appendSlice(gpa, " (active project's server)");
        }
        try out.appendSlice(gpa, ".");
    }
    if (active_project.len != 0) {
        try appendFmt(gpa, &out, "\nActive server project: \"{s}\".", .{active_project});
    } else {
        try out.appendSlice(gpa, "\nActive server project: none.");
    }
    for (servers) |s| {
        const names = s.projects orelse continue; // unknown (unreachable) ≠ empty
        try appendFmt(gpa, &out, "\nProjects on {s}: ", .{s.url});
        if (names.len == 0) {
            try out.appendSlice(gpa, "(none)");
            continue;
        }
        const shown = @min(names.len, max_context_projects);
        for (names[0..shown], 0..) |n, i| {
            if (i != 0) try out.appendSlice(gpa, ", ");
            try out.appendSlice(gpa, n);
        }
        if (names.len > shown) try appendFmt(gpa, &out, " (+{d} more)", .{names.len - shown});
        try out.appendSlice(gpa, ".");
    }
    return out.toOwnedSlice(gpa);
}

fn appendFmt(gpa: std.mem.Allocator, out: *std.ArrayList(u8), comptime fmt: []const u8, fargs: anytype) error{OutOfMemory}!void {
    const s = try std.fmt.allocPrint(gpa, fmt, fargs);
    defer gpa.free(s);
    try out.appendSlice(gpa, s);
}

/// Sanitize a variant label into a `[a-z0-9-]` file stem (runs of other characters become
/// single dashes; capped at 40 chars; dangling dashes trimmed). May come out empty —
/// callers fall back to the variant's 1-based position. Caller owns the result.
pub fn sanitizeLabel(gpa: std.mem.Allocator, label: []const u8) error{OutOfMemory}![]u8 {
    var out: std.ArrayList(u8) = .empty;
    errdefer out.deinit(gpa);
    for (label) |raw| {
        const c = std.ascii.toLower(raw);
        if (std.ascii.isLower(c) or std.ascii.isDigit(c)) {
            if (out.items.len >= max_label_chars) break;
            try out.append(gpa, c);
        } else if (out.items.len != 0 and out.items[out.items.len - 1] != '-') {
            try out.append(gpa, '-');
        }
    }
    while (out.items.len != 0 and out.items[out.items.len - 1] == '-') _ = out.pop();
    return out.toOwnedSlice(gpa);
}

// ── tests ────────────────────────────────────────────────────────────────────

const testing = std.testing;

// The plan returned for `raw`, which the test expects to be VALID (not `.invalid`).
fn mustPlan(raw: []const u8) !Plan {
    switch (try parsePlan(testing.allocator, raw)) {
        .plan => |p| return p,
        .invalid => |msg| {
            defer testing.allocator.free(msg);
            std.debug.print("unexpectedly invalid: {s}\n", .{msg});
            return error.TestUnexpectedResult;
        },
    }
}

// The rejection message for `raw`, which the test expects to be INVALID.
fn mustReject(raw: []const u8, expected: []const u8) !void {
    switch (try parsePlan(testing.allocator, raw)) {
        .plan => |p| {
            var plan = p;
            defer plan.deinit();
            std.debug.print("unexpectedly valid for: {s}\n", .{raw});
            return error.TestUnexpectedResult;
        },
        .invalid => |msg| {
            defer testing.allocator.free(msg);
            try testing.expectEqualStrings(expected, msg);
        },
    }
}

test "isCropToken / isPageFormat: grammar edges" {
    try testing.expect(isCropToken("10"));
    try testing.expect(isCropToken("-10%"));
    try testing.expect(isCropToken(".5in"));
    try testing.expect(isCropToken("1.5cm"));
    try testing.expect(!isCropToken(""));
    try testing.expect(!isCropToken("-"));
    try testing.expect(!isCropToken("1.")); // a dot needs a fraction
    try testing.expect(!isCropToken("1..5"));
    try testing.expect(!isCropToken("10 %"));
    try testing.expect(!isCropToken("10pt"));

    try testing.expect(isPageFormat("a0"));
    try testing.expect(isPageFormat("b10"));
    try testing.expect(!isPageFormat("a")); // needs a number
    try testing.expect(!isPageFormat("a11")); // 0–10 only
    try testing.expect(!isPageFormat("d4")); // a/b/c series only
}

test "parsePlan: chat-only fallback (no JSON object, or braces that are not JSON)" {
    var p1 = try mustPlan("  Just chatting, no ops needed.  ");
    defer p1.deinit();
    try testing.expect(p1.chat_only);
    try testing.expectEqualStrings("Just chatting, no ops needed.", p1.reply);
    try testing.expectEqual(@as(usize, 0), p1.actions.len);
    try testing.expectEqual(@as(usize, 0), p1.variants.len);

    var p2 = try mustPlan("some text with {braces that are not json");
    defer p2.deinit();
    try testing.expect(p2.chat_only);

    var p3 = try mustPlan("{ not: json }");
    defer p3.deinit();
    try testing.expect(p3.chat_only);
}

test "parsePlan: fence stripping + first balanced object + surrounding prose" {
    var p = try mustPlan(
        \\Sure! Here is the plan:
        \\```json
        \\{"version":1,"reply":"rotating","actions":[{"op":"rotate","dir":"left"}]}
        \\```
        \\Anything else?
    );
    defer p.deinit();
    try testing.expect(!p.chat_only);
    try testing.expectEqualStrings("rotating", p.reply);
    try testing.expectEqual(@as(usize, 1), p.actions.len);
    try testing.expect(p.actions[0].rotate.dir == .left);
    try testing.expectEqual(@as(u8, 1), p.actions[0].rotate.times); // default

    // Braces inside strings don't confuse the balanced-object scanner.
    var p2 = try mustPlan("{\"version\":2,\"reply\":\"ok {see}\",\"actions\":[]}");
    defer p2.deinit();
    try testing.expectEqualStrings("ok {see}", p2.reply); // version 2 accepted but ignored
}

test "parsePlan: unknown op drops with a warning; known op with bad params rejects" {
    var p = try mustPlan("{\"reply\":\"ok\",\"actions\":[{\"op\":\"resize\",\"w\":100},{\"op\":\"filter\",\"mode\":\"bw\"}]}");
    defer p.deinit();
    try testing.expectEqual(@as(usize, 1), p.actions.len); // resize dropped, filter kept
    try testing.expect(p.actions[0].filter.mode == .bw);
    try testing.expectEqual(@as(usize, 1), p.warnings.len);
    try testing.expectEqualStrings("Skipped unknown operation \"resize\"", p.warnings[0]);

    try mustReject(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"rotate\",\"dir\":\"left\",\"times\":5}]}",
        "invalid rotate action: \"times\" must be an integer 1..3",
    );
    try mustReject(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"rotate\",\"dir\":\"up\"}]}",
        "invalid rotate action: \"dir\" must be \"left\" or \"right\"",
    );
    // Unknown fields on a KNOWN op fail the plan (strict validation).
    try mustReject(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"rotate\",\"dir\":\"left\",\"angle\":45}]}",
        "invalid rotate action: unknown field \"angle\"",
    );
    // §1 reply tolerance: "Done." + a warning, the plan itself survives.
    {
        var tolerated = try mustPlan("{\"actions\":[{\"op\":\"rotate\",\"dir\":\"left\"}]}");
        defer tolerated.deinit();
        try testing.expectEqualStrings("Done.", tolerated.reply);
        try testing.expectEqual(@as(usize, 1), tolerated.actions.len);
        try testing.expectEqual(@as(usize, 1), tolerated.warnings.len);
        try testing.expectEqualStrings("The model omitted its reply — the plan still ran", tolerated.warnings[0]);
    }
    {
        // An EMPTY plan says so — a bare "Done." would read as a success that
        // never occurred (contract §1).
        var blank_reply = try mustPlan("{\"reply\":\"  \"}");
        defer blank_reply.deinit();
        try testing.expectEqualStrings("The model returned an empty plan — nothing was changed.", blank_reply.reply);
        try testing.expectEqual(@as(usize, 0), blank_reply.warnings.len);
    }
    try mustReject("{\"reply\":\"ok\",\"actions\":{}}", "invalid plan: \"actions\" must be an array");
}

test "parsePlan: crop spec validation" {
    var p = try mustPlan("{\"reply\":\"ok\",\"actions\":[{\"op\":\"crop\",\"spec\":{\"x1\":\"10%\",\"x2\":\"-1.5cm\",\"y1\":\"0\",\"y2\":\"90px\"}}]}");
    defer p.deinit();
    try testing.expectEqualStrings("10%", p.actions[0].crop.x1.?);
    try testing.expectEqualStrings("-1.5cm", p.actions[0].crop.x2.?);

    try mustReject(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"crop\",\"spec\":{}}]}",
        "invalid crop action: spec needs at least one of x1/x2/y1/y2/aspect",
    );
    try mustReject(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"crop\",\"spec\":{\"left\":\"10%\"}}]}",
        "invalid crop action: unknown spec key \"left\"",
    );
    try mustReject(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"crop\",\"spec\":{\"x1\":\"10q\"}}]}",
        "invalid crop action: bad token for \"x1\"",
    );
    try mustReject(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"crop\",\"spec\":{\"x1\":12}}]}",
        "invalid crop action: bad token for \"x1\"",
    );
}

test "parsePlan: crop aspect — strict W:H, digits only, both positive" {
    var p = try mustPlan("{\"reply\":\"ok\",\"actions\":[{\"op\":\"crop\",\"spec\":{\"x1\":\"10%\",\"aspect\":\"4:3\"}}]}");
    defer p.deinit();
    try testing.expectEqualStrings("10%", p.actions[0].crop.x1.?);
    try testing.expectEqualStrings("4:3", p.actions[0].crop.aspect.?);

    // Aspect alone satisfies the at-least-one-key rule.
    var alone = try mustPlan("{\"reply\":\"ok\",\"actions\":[{\"op\":\"crop\",\"spec\":{\"aspect\":\"16:9\"}}]}");
    defer alone.deinit();
    try testing.expectEqualStrings("16:9", alone.actions[0].crop.aspect.?);
    try testing.expect(alone.actions[0].crop.x1 == null);

    // Malformed aspect = invalid params on a known op = the whole plan fails.
    inline for (.{ "0:3", "4:0", "-1:2", "4:-3", "3:4:5", "a:b", "1.5:2", "4", "4:", ":3", "1e2:3", "" }) |aspect| {
        try mustReject(
            "{\"reply\":\"ok\",\"actions\":[{\"op\":\"crop\",\"spec\":{\"aspect\":\"" ++ aspect ++ "\"}}]}",
            "invalid crop action: bad token for \"aspect\"",
        );
    }
    try mustReject(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"crop\",\"spec\":{\"aspect\":43}}]}",
        "invalid crop action: bad token for \"aspect\"",
    );
}

test "parsePlan: action-level crop aspect folds into the spec; a conflicting duplicate fails" {
    // The tolerated spelling: "aspect" BESIDE "spec" — folded in, same validation.
    var beside = try mustPlan("{\"reply\":\"ok\",\"actions\":[{\"op\":\"crop\",\"spec\":{\"x1\":\"10%\"},\"aspect\":\"3:4\"}]}");
    defer beside.deinit();
    try testing.expectEqualStrings("10%", beside.actions[0].crop.x1.?);
    try testing.expectEqualStrings("3:4", beside.actions[0].crop.aspect.?);

    // Beside an EMPTY spec it still satisfies the at-least-one-key rule.
    var alone = try mustPlan("{\"reply\":\"ok\",\"actions\":[{\"op\":\"crop\",\"spec\":{},\"aspect\":\"16:9\"}]}");
    defer alone.deinit();
    try testing.expectEqualStrings("16:9", alone.actions[0].crop.aspect.?);

    // An EQUAL duplicate folds silently; the spec's own value wins the fold.
    var equal = try mustPlan("{\"reply\":\"ok\",\"actions\":[{\"op\":\"crop\",\"spec\":{\"aspect\":\"1:1\"},\"aspect\":\"1:1\"}]}");
    defer equal.deinit();
    try testing.expectEqualStrings("1:1", equal.actions[0].crop.aspect.?);

    // Conflicting duplicates = invalid params on a known op = the whole plan fails.
    try mustReject(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"crop\",\"spec\":{\"aspect\":\"3:4\"},\"aspect\":\"4:3\"}]}",
        "invalid crop action: \"aspect\" appears both beside \"spec\" and inside it with different values",
    );
    // The action-level spelling gets the same W:H validation …
    try mustReject(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"crop\",\"spec\":{\"x1\":\"10%\"},\"aspect\":\"0:3\"}]}",
        "invalid crop action: bad token for \"aspect\"",
    );
    try mustReject(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"crop\",\"spec\":{\"x1\":\"10%\"},\"aspect\":43}]}",
        "invalid crop action: bad token for \"aspect\"",
    );
    // … and other stray fields on crop still fail the plan.
    try mustReject(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"crop\",\"spec\":{\"x1\":\"10%\"},\"ratio\":\"3:4\"}]}",
        "invalid crop action: unknown field \"ratio\"",
    );
}

test "parsePlan: filter tint rules, formula charset, page + blank formats, frame exclusivity" {
    var p = try mustPlan("{\"reply\":\"ok\",\"actions\":[{\"op\":\"filter\",\"mode\":\"custom\",\"tint\":\"#A1b2c3\"},{\"op\":\"formula\",\"axis\":\"y\",\"expr\":\"y*2 + 1\"},{\"op\":\"page\",\"format\":\"c10\"},{\"op\":\"blank\",\"color\":\"#ffffff\",\"format\":\"a4\"}]}");
    defer p.deinit();
    try testing.expectEqualStrings("#A1b2c3", p.actions[0].filter.tint);
    try testing.expectEqual(@as(u8, 'y'), p.actions[1].formula.axis);
    try testing.expectEqualStrings("c10", p.actions[2].page.format);
    try testing.expectEqualStrings("a4", p.actions[3].blank.format.?);

    try mustReject(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"filter\",\"mode\":\"custom\"}]}",
        "invalid filter action: \"custom\" requires \"tint\" as #rrggbb",
    );
    try mustReject(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"filter\",\"mode\":\"bw\",\"tint\":\"#112233\"}]}",
        "invalid filter action: \"tint\" is only valid with mode \"custom\"",
    );
    try mustReject(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"formula\",\"axis\":\"x\",\"expr\":\"y*2\"}]}",
        "invalid formula action: \"expr\" may only use digits, + - * / ( ) . and \"x\"",
    );
    try mustReject(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"page\",\"format\":\"A4\"}]}", // lowercase only
        "invalid page action: \"format\" must be a lowercase ISO name a0\u{2013}a10, b0\u{2013}b10 or c0\u{2013}c10",
    );
    try mustReject(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"blank\",\"color\":\"#12345\"}]}",
        "invalid blank action: \"color\" must be #rrggbb or a CSS color name",
    );
    try mustReject(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"frame\",\"index\":0,\"indices\":[1]}]}",
        "invalid frame action: exactly one of \"index\" / \"indices\" is required",
    );
    try mustReject(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"frame\",\"indices\":[]}]}",
        "invalid frame action: \"indices\" must be a non-empty array",
    );

    var pf = try mustPlan("{\"reply\":\"ok\",\"actions\":[{\"op\":\"frame\",\"index\":3}]}");
    defer pf.deinit();
    try testing.expectEqualSlices(u32, &.{3}, pf.actions[0].frame.indices);
    try testing.expect(pf.hasFrameOp());
}

test "parsePlan: layout line validation + re-serialization" {
    var p = try mustPlan(
        \\{"reply":"drawn","actions":[{"op":"layout","lines":[
        \\  {"points":[{"x":1,"y":2},{"x":3,"y":4}],"color":"#FF0000","style":"dashed","locked":true}
        \\]}]}
    );
    defer p.deinit();
    // The validated lines round-trip as a JSON array the /apply path consumes directly.
    try testing.expectEqualStrings(
        "[{\"points\":[{\"x\":1,\"y\":2},{\"x\":3,\"y\":4}],\"color\":\"#FF0000\",\"style\":\"dashed\",\"locked\":true}]",
        p.actions[0].layout.lines_json,
    );

    try mustReject(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"layout\",\"lines\":[{\"points\":[{\"x\":1,\"y\":2}],\"style\":\"wavy\"}]}]}",
        "invalid layout action: unknown line style",
    );
    try mustReject(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"layout\",\"lines\":[{\"points\":[{\"x\":1,\"z\":2}]}]}]}",
        "invalid layout action: points must be {x, y} objects",
    );
    try mustReject(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"layout\",\"lines\":[{\"points\":[{\"x\":1,\"y\":2}],\"glow\":true}]}]}",
        "invalid layout action: unknown line field \"glow\"",
    );
}

test "parsePlan: limits (16 actions, 8 variants, 200 lines, 5000-char strings)" {
    const a = testing.allocator;

    // 17 actions → rejected.
    var many: std.ArrayList(u8) = .empty;
    defer many.deinit(a);
    try many.appendSlice(a, "{\"reply\":\"ok\",\"actions\":[");
    for (0..17) |i| {
        if (i != 0) try many.append(a, ',');
        try many.appendSlice(a, "{\"op\":\"rotate\",\"dir\":\"left\"}");
    }
    try many.appendSlice(a, "]}");
    try mustReject(many.items, "invalid plan: more than 16 actions in \"actions\"");

    // 9 variants → rejected.
    var vars: std.ArrayList(u8) = .empty;
    defer vars.deinit(a);
    try vars.appendSlice(a, "{\"reply\":\"ok\",\"variants\":[");
    for (0..9) |i| {
        if (i != 0) try vars.append(a, ',');
        try vars.appendSlice(a, "{\"label\":\"v\",\"actions\":[]}");
    }
    try vars.appendSlice(a, "]}");
    try mustReject(vars.items, "invalid plan: more than 8 variants");

    // 201 layout lines → rejected.
    var lines: std.ArrayList(u8) = .empty;
    defer lines.deinit(a);
    try lines.appendSlice(a, "{\"reply\":\"ok\",\"actions\":[{\"op\":\"layout\",\"lines\":[");
    for (0..201) |i| {
        if (i != 0) try lines.append(a, ',');
        try lines.appendSlice(a, "{\"points\":[{\"x\":0,\"y\":0}]}");
    }
    try lines.appendSlice(a, "]}]}");
    try mustReject(lines.items, "invalid layout action: more than 200 lines");

    // A formula expression over 5000 chars → rejected (per-string cap).
    var expr: std.ArrayList(u8) = .empty;
    defer expr.deinit(a);
    try expr.appendSlice(a, "{\"reply\":\"ok\",\"actions\":[{\"op\":\"formula\",\"axis\":\"x\",\"expr\":\"");
    for (0..5001) |_| try expr.append(a, 'x');
    try expr.appendSlice(a, "\"}]}");
    try mustReject(expr.items, "invalid formula action: \"expr\" must be a string");
}

test "parsePlan: variants (labels, defaults, per-variant actions + frame detection)" {
    var p = try mustPlan(
        \\{"reply":"4 takes","actions":[],"variants":[
        \\  {"label":"Rotated!","actions":[{"op":"rotate","dir":"right","times":2}]},
        \\  {"actions":[{"op":"filter","mode":"sepia"}]}
        \\]}
    );
    defer p.deinit();
    try testing.expectEqual(@as(usize, 2), p.variants.len);
    try testing.expectEqualStrings("Rotated!", p.variants[0].label);
    try testing.expectEqualStrings("", p.variants[1].label); // empty → positional file name
    try testing.expectEqual(@as(u8, 2), p.variants[0].actions[0].rotate.times);
    try testing.expect(!p.hasFrameOp());

    var pf = try mustPlan("{\"reply\":\"ok\",\"variants\":[{\"actions\":[{\"op\":\"frame\",\"index\":1}]}]}");
    defer pf.deinit();
    try testing.expect(pf.hasFrameOp()); // frame inside a variant is still a plan-level error

    try mustReject(
        "{\"reply\":\"ok\",\"variants\":[{\"label\":7}]}",
        "invalid plan: variant \"label\" must be a string",
    );
    try mustReject(
        "{\"reply\":\"ok\",\"variants\":[{\"actions\":[{\"op\":\"rotate\"}]}]}",
        "invalid rotate action: \"dir\" must be \"left\" or \"right\"",
    );
}

test "sanitizeLabel + cropSpecString" {
    const a = testing.allocator;

    const s1 = try sanitizeLabel(a, "Rotated & Tinted (v2)");
    defer a.free(s1);
    try testing.expectEqualStrings("rotated-tinted-v2", s1);
    const s2 = try sanitizeLabel(a, "___");
    defer a.free(s2);
    try testing.expectEqualStrings("", s2); // callers fall back to the position
    const s3 = try sanitizeLabel(a, "a" ** 60);
    defer a.free(s3);
    try testing.expectEqual(@as(usize, max_label_chars), s3.len);

    const spec = try cropSpecString(a, .{ .x1 = "10%", .y2 = "-2cm" });
    defer a.free(spec);
    try testing.expectEqualStrings("x1=10% y2=-2cm", spec);

    // The aspect ratio rides along as its own `k=v` token, resolved core-side.
    const with_aspect = try cropSpecString(a, .{ .x1 = "10%", .aspect = "4:3" });
    defer a.free(with_aspect);
    try testing.expectEqualStrings("x1=10% aspect=4:3", with_aspect);
}

// ── §11 interactive replies (`ask`) ─────────────────────────────────────────

/// Parse a plan carrying an `ask`, returning the card (caller deinits the plan).
fn parseAsk(a: std.mem.Allocator, json: []const u8) !Plan {
    switch (try parsePlan(a, json)) {
        .invalid => |m| {
            a.free(m);
            return error.TestUnexpectedInvalid;
        },
        .plan => |p| return p,
    }
}

/// Assert a plan is REJECTED (a malformed card is a plan error, not a silent drop).
fn expectAskInvalid(a: std.mem.Allocator, json: []const u8) !void {
    switch (try parsePlan(a, json)) {
        .invalid => |m| a.free(m),
        .plan => |p| {
            var plan = p;
            plan.deinit();
            return error.TestExpectedInvalid;
        },
    }
}

test "parsePlan: ask card, defaults, and multi mode" {
    const a = testing.allocator;

    var p = try parseAsk(a,
        \\{"version":1,"reply":"pick one","ask":{"question":"Which tint?","options":[{"label":"Sepia"},{"label":"B&W"}]}}
    );
    defer p.deinit();
    const ask = p.ask.?;
    try testing.expectEqualStrings("Which tint?", ask.question);
    try testing.expect(!ask.multi); // default single
    try testing.expect(!ask.allow_custom);
    try testing.expectEqualStrings(default_custom_label, ask.custom_label);
    try testing.expectEqual(@as(usize, 2), ask.options.len);
    try testing.expectEqualStrings("Sepia", ask.options[0].label);

    var m = try parseAsk(a,
        \\{"version":1,"reply":"pick some","ask":{"question":" Which? ","mode":"multi","allowCustom":true,"customLabel":" Other ","options":[{"label":" A "},{"label":"B"}]}}
    );
    defer m.deinit();
    try testing.expect(m.ask.?.multi);
    try testing.expect(m.ask.?.allow_custom);
    try testing.expectEqualStrings("Which?", m.ask.?.question); // trimmed
    try testing.expectEqualStrings("Other", m.ask.?.custom_label);
    try testing.expectEqualStrings("A", m.ask.?.options[0].label);
}

test "parsePlan: no ask on an ordinary or chat-only turn" {
    const a = testing.allocator;
    var p = try parseAsk(a,
        \\{"version":1,"reply":"hi","actions":[]}
    );
    defer p.deinit();
    try testing.expect(p.ask == null);

    var c = try parseAsk(a, "just chatting");
    defer c.deinit();
    try testing.expect(c.ask == null);
    try testing.expect(c.chat_only);
}

test "parsePlan: a console drops option previews with ONE note, never the option" {
    const a = testing.allocator;
    var p = try parseAsk(a,
        \\{"version":1,"reply":"pick","ask":{"question":"Which?","options":[{"label":"Sepia","actions":[{"op":"filter","mode":"sepia"}]},{"label":"Web","image":{"url":"https://e/x.png"}}]}}
    );
    defer p.deinit();
    try testing.expectEqual(@as(usize, 2), p.ask.?.options.len); // both kept
    try testing.expectEqualStrings("Sepia", p.ask.?.options[0].label);
    var notes: usize = 0;
    for (p.warnings) |w| {
        if (std.mem.indexOf(u8, w, "can't show option previews") != null) notes += 1;
    }
    try testing.expectEqual(@as(usize, 1), notes); // one per CARD, not per option
}

test "parsePlan: a misplaced op in an option preview never costs the option (§1/§11.2)" {
    const a = testing.allocator;
    var p = try parseAsk(a,
        \\{"version":1,"reply":"pick","ask":{"question":"Which?","options":[{"label":"Wipe","actions":[{"op":"clear"}]},{"label":"Sepia","actions":[{"op":"filter","mode":"sepia"}]}]}}
    );
    defer p.deinit();
    try testing.expectEqual(@as(usize, 2), p.ask.?.options.len); // both kept, both previewless
    try testing.expectEqualStrings("Wipe", p.ask.?.options[0].label);
}

test "parsePlan: malformed ask cards are plan errors" {
    const a = testing.allocator;
    try expectAskInvalid(a,
        \\{"version":1,"reply":"x","ask":"hello"}
    );
    try expectAskInvalid(a,
        \\{"version":1,"reply":"x","ask":{"options":[{"label":"A"},{"label":"B"}]}}
    );
    try expectAskInvalid(a,
        \\{"version":1,"reply":"x","ask":{"question":"   ","options":[{"label":"A"},{"label":"B"}]}}
    );
    try expectAskInvalid(a,
        \\{"version":1,"reply":"x","ask":{"question":"Q","options":[]}}
    );
    try expectAskInvalid(a,
        \\{"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"only"}]}}
    );
    try expectAskInvalid(a,
        \\{"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"1"},{"label":"2"},{"label":"3"},{"label":"4"},{"label":"5"},{"label":"6"}]}}
    );
    try expectAskInvalid(a,
        \\{"version":1,"reply":"x","ask":{"question":"Q","mode":"maybe","options":[{"label":"A"},{"label":"B"}]}}
    );
    try expectAskInvalid(a,
        \\{"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"A"},{"label":"B"}],"sneaky":1}}
    );
    try expectAskInvalid(a,
        \\{"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"A","sneaky":1},{"label":"B"}]}}
    );
    try expectAskInvalid(a,
        \\{"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":""},{"label":"B"}]}}
    );
    try expectAskInvalid(a,
        \\{"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"A","actions":[],"image":{"url":"https://e/x"}},{"label":"B"}]}}
    );
    try expectAskInvalid(a,
        \\{"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"A"},{"label":"B"}],"allowCustom":"yes"}}
    );
}

test "resolveAskAnswer: a number picks a label; anything else stays plain text" {
    const a = testing.allocator;
    var opts = [_][]u8{
        try a.dupe(u8, "Sepia"), try a.dupe(u8, "B&W"), try a.dupe(u8, "Blue"),
    };
    defer for (opts) |o| a.free(o);

    const one = (try resolveAskAnswer(a, &opts, false, " 2 ")).?;
    defer a.free(one);
    try testing.expectEqualStrings("B&W", one);

    // Several numbers at a pick-ONE card is not a selection — it goes as typed.
    try testing.expect((try resolveAskAnswer(a, &opts, false, "1,3")) == null);

    const many = (try resolveAskAnswer(a, &opts, true, "1, 3")).?;
    defer a.free(many);
    try testing.expectEqualStrings("Sepia, Blue", many);

    const spaced = (try resolveAskAnswer(a, &opts, true, "3 1")).?;
    defer a.free(spaced);
    try testing.expectEqualStrings("Blue, Sepia", spaced); // the user's order is kept

    const dedup = (try resolveAskAnswer(a, &opts, true, "2,2")).?;
    defer a.free(dedup);
    try testing.expectEqualStrings("B&W", dedup); // a repeat is one pick

    // Out of range, non-numeric, empty, and no card at all → plain text (never an error).
    try testing.expect((try resolveAskAnswer(a, &opts, false, "0")) == null);
    try testing.expect((try resolveAskAnswer(a, &opts, false, "4")) == null);
    try testing.expect((try resolveAskAnswer(a, &opts, true, "1,9")) == null);
    try testing.expect((try resolveAskAnswer(a, &opts, false, "make it warmer")) == null);
    try testing.expect((try resolveAskAnswer(a, &opts, false, "   ")) == null);
    var none = [_][]u8{};
    try testing.expect((try resolveAskAnswer(a, &none, false, "1")) == null);
}

test "parsePlan: §2.1 image/save shapes, and both are top-level only" {
    var p = try mustPlan(
        \\{"reply":"three","actions":[{"op":"image","index":2},
        \\  {"op":"save","name":"portrait 1"},{"op":"save"}]}
    );
    defer p.deinit();
    try testing.expectEqual(@as(usize, 3), p.actions.len);
    try testing.expectEqual(@as(u32, 2), p.actions[0].image.index);
    try testing.expectEqualStrings("portrait 1", p.actions[1].save.name);
    try testing.expectEqualStrings("", p.actions[2].save.name); // derived at execution

    // index is 1-based: 0, negatives, non-integers and strings are not an attachment.
    const bad_index = [_][]const u8{ "0", "-1", "1.5", "\"1\"", "null", "true" };
    for (bad_index) |v| {
        const raw = try std.fmt.allocPrint(testing.allocator, "{{\"reply\":\"ok\",\"actions\":[{{\"op\":\"image\",\"index\":{s}}}]}}", .{v});
        defer testing.allocator.free(raw);
        try mustReject(raw, "invalid image action: \"index\" must be an integer >= 1");
    }
    try mustReject(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"image\",\"index\":1,\"name\":\"x\"}]}",
        "invalid image action: unknown field \"name\"",
    );

    // A save name is bounded (§2.1) and must be a string; unknown fields fail the plan.
    {
        const long = try std.fmt.allocPrint(testing.allocator, "{{\"reply\":\"ok\",\"actions\":[{{\"op\":\"save\",\"name\":\"{s}\"}}]}}", .{"x" ** 121});
        defer testing.allocator.free(long);
        try mustReject(long, "invalid save action: \"name\" must be a string of at most 120 characters");
    }
    try mustReject(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"save\",\"name\":7}]}",
        "invalid save action: \"name\" must be a string of at most 120 characters",
    );
    // §10: a save may carry a destination — a folder or a file name — which the executor
    // accepts only if the USER wrote it. A URL is never a destination (openUrl owns those).
    {
        var dest = try mustPlan("{\"reply\":\"ok\",\"actions\":[{\"op\":\"save\",\"path\":\"~/Downloads\"}]}");
        defer dest.deinit();
        try testing.expectEqualStrings("~/Downloads", dest.actions[0].save.path);
    }
    try mustReject(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"save\",\"path\":\"https://x.example/out.png\"}]}",
        "invalid save action: \"path\" is a local path, not a URL",
    );

    // …and neither may hide inside a variant (which renders ONE image's alternatives):
    // that variant is dropped with a note, never the whole plan (§1/§2.1).
    inline for (.{ "{\"op\":\"image\",\"index\":1}", "{\"op\":\"save\"}" }, .{ "image", "save" }) |op_json, name| {
        var dropped = try mustPlan("{\"reply\":\"ok\",\"variants\":[{\"actions\":[" ++ op_json ++ "]}]}");
        defer dropped.deinit();
        try testing.expectEqual(@as(usize, 0), dropped.variants.len);
        try testing.expectEqual(@as(usize, 1), dropped.warnings.len);
        try testing.expectEqualStrings(
            "Dropped variant 1: the top-level-only op \"" ++ name ++ "\" can't run inside a variant — the rest of the plan ran",
            dropped.warnings[0],
        );
    }
}

test "parsePlan: console ops validate (accent hex, connect/disconnect server, delete path)" {
    var p = try mustPlan(
        \\{"reply":"done","actions":[{"op":"accent","color":"#00FFff"},
        \\  {"op":"connect","server":"http://a:8090"},{"op":"disconnect","server":" a "},
        \\  {"op":"delete","path":"old.stencil"}]}
    );
    defer p.deinit();
    try testing.expectEqual(@as(usize, 4), p.actions.len);
    try testing.expectEqualStrings("#00FFff", p.actions[0].accent.color);
    try testing.expectEqualStrings("http://a:8090", p.actions[1].connect.server);
    try testing.expectEqualStrings("a", p.actions[2].disconnect.server); // trimmed
    try testing.expectEqualStrings("old.stencil", p.actions[3].delete.path);
    // None of the console ops trips the frame/image plan-level checks or a continuation.
    try testing.expect(!p.hasFrameOp() and !p.loadsWithoutTracing());
}

test "parsePlan: console ops with bad params reject the whole plan" {
    // accent takes exactly a #rrggbb hex — names and shorthand hexes are the model's to translate.
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"accent\",\"color\":\"cyan\"}]}", "invalid accent action: \"color\" must be #rrggbb");
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"accent\",\"color\":\"#0ff\"}]}", "invalid accent action: \"color\" must be #rrggbb");
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"accent\"}]}", "invalid accent action: exactly one of \"color\" / \"preset\" is required");
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"accent\",\"color\":\"#00ffff\",\"mode\":\"dark\"}]}", "invalid accent action: unknown field \"mode\"");
    // connect/disconnect need a non-empty server; delete a non-empty path.
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"connect\",\"server\":\"\"}]}", "invalid connect action: \"server\" must be a non-empty string");
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"connect\"}]}", "invalid connect action: \"server\" must be a non-empty string");
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"disconnect\",\"server\":3}]}", "invalid disconnect action: \"server\" must be a non-empty string");
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"connect\",\"server\":\"a\",\"token\":\"t\"}]}", "invalid connect action: unknown field \"token\"");
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"delete\",\"path\":\" \"}]}", "invalid delete action: \"path\" must be a non-empty string");
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"delete\",\"name\":\"x\"}]}", "invalid delete action: unknown field \"name\"");
}

test "parsePlan: console ops are top-level only — a variant carrying one is dropped" {
    const inside = [_][]const u8{
        "{\"op\":\"accent\",\"color\":\"#00ffff\"}",
        "{\"op\":\"connect\",\"server\":\"a\"}",
        "{\"op\":\"disconnect\",\"server\":\"a\"}",
        "{\"op\":\"reconnect\",\"server\":\"a\"}",
        "{\"op\":\"delete\",\"path\":\"x.stencil\"}",
        "{\"op\":\"openUrl\",\"url\":\"https://a.example/x.png\"}",
        "{\"op\":\"copy\"}",
        "{\"op\":\"clear\"}",
    };
    const names = [_][]const u8{ "accent", "connect", "disconnect", "reconnect", "delete", "openUrl", "copy", "clear" };
    for (inside, names) |op_json, name| {
        const raw = try std.fmt.allocPrint(testing.allocator, "{{\"reply\":\"x\",\"variants\":[{{\"actions\":[{s}]}}]}}", .{op_json});
        defer testing.allocator.free(raw);
        var p = try mustPlan(raw);
        defer p.deinit();
        const expected = try std.fmt.allocPrint(
            testing.allocator,
            "Dropped variant 1: the console-settings op \"{s}\" can't run inside a variant — the rest of the plan ran",
            .{name},
        );
        defer testing.allocator.free(expected);
        try testing.expectEqual(@as(usize, 0), p.variants.len);
        try testing.expectEqual(@as(usize, 1), p.warnings.len);
        try testing.expectEqualStrings(expected, p.warnings[0]);
    }
    // §2's undo/redo/reset are top-level only too — a distinct (§2.1-style) reason.
    inline for (.{ "undo", "redo", "reset" }) |op| {
        var p = try mustPlan("{\"reply\":\"x\",\"variants\":[{\"actions\":[{\"op\":\"" ++ op ++ "\"}]}]}");
        defer p.deinit();
        try testing.expectEqual(@as(usize, 0), p.variants.len);
        try testing.expectEqualStrings(
            "Dropped variant 1: the top-level-only op \"" ++ op ++ "\" can't run inside a variant — the rest of the plan ran",
            p.warnings[0],
        );
    }
}

test "parsePlan: a misplaced op costs its variant, not the plan (contract §1)" {
    // Good top-level actions + a bad variant + a good one: everything else survives,
    // and the note names the dropped variant by position AND label.
    var p = try mustPlan(
        \\{"reply":"two takes","actions":[{"op":"rotate","dir":"right"}],"variants":[
        \\  {"label":"Wiped","actions":[{"op":"filter","mode":"bw"},{"op":"clear"}]},
        \\  {"label":"Sepia","actions":[{"op":"filter","mode":"sepia"}]}
        \\]}
    );
    defer p.deinit();
    try testing.expectEqual(@as(usize, 1), p.actions.len);
    try testing.expect(p.actions[0] == .rotate);
    try testing.expectEqual(@as(usize, 1), p.variants.len);
    try testing.expectEqualStrings("Sepia", p.variants[0].label);
    try testing.expectEqual(@as(usize, 1), p.warnings.len);
    try testing.expectEqualStrings(
        "Dropped variant 1 (\"Wiped\"): the console-settings op \"clear\" can't run inside a variant — the rest of the plan ran",
        p.warnings[0],
    );

    // A plan whose ONLY content was such a variant is still a normal reply + note.
    var only = try mustPlan("{\"reply\":\"cleared\",\"variants\":[{\"actions\":[{\"op\":\"clearChat\"}]}]}");
    defer only.deinit();
    try testing.expectEqualStrings("cleared", only.reply);
    try testing.expectEqual(@as(usize, 0), only.actions.len);
    try testing.expectEqual(@as(usize, 0), only.variants.len);
    try testing.expectEqualStrings(
        "Dropped variant 1: the console-settings op \"clearChat\" can't run inside a variant — the rest of the plan ran",
        only.warnings[0],
    );

    // The other strictness is untouched: a known op with bad params inside a variant
    // still fails the plan, an unknown one is still just skipped, and a forbidden name
    // is still rejected outright (§13).
    try mustReject(
        "{\"reply\":\"x\",\"variants\":[{\"actions\":[{\"op\":\"rotate\",\"dir\":\"up\"}]}]}",
        "invalid rotate action: \"dir\" must be \"left\" or \"right\"",
    );
    try mustReject(
        "{\"reply\":\"x\",\"variants\":[{\"actions\":[{\"op\":\"chatPersist\",\"on\":true}]}]}",
        "invalid plan: the \"chatPersist\" op is never model-drivable",
    );
    var unknown = try mustPlan("{\"reply\":\"x\",\"variants\":[{\"actions\":[{\"op\":\"resize\",\"w\":2},{\"op\":\"filter\",\"mode\":\"bw\"}]}]}");
    defer unknown.deinit();
    try testing.expectEqual(@as(usize, 1), unknown.variants.len);
    try testing.expectEqualStrings("Skipped unknown operation \"resize\"", unknown.warnings[0]);
}

test "parsePlan: §2 undo/redo/reset shapes — steps 1..20, default 1, reset fieldless" {
    var p = try mustPlan(
        \\{"reply":"back","actions":[{"op":"undo"},{"op":"redo","steps":3},
        \\  {"op":"undo","steps":20},{"op":"reset"}]}
    );
    defer p.deinit();
    try testing.expectEqual(@as(usize, 4), p.actions.len);
    try testing.expectEqual(@as(u8, 1), p.actions[0].undo.steps); // default
    try testing.expectEqual(@as(u8, 3), p.actions[1].redo.steps);
    try testing.expectEqual(@as(u8, 20), p.actions[2].undo.steps);
    try testing.expect(p.actions[3] == .reset);
    // None of them trips the plan-level frame/image checks or a §7 continuation.
    try testing.expect(!p.hasFrameOp() and !p.loadsWithoutTracing());

    // steps out of range / non-integer, and stray fields, fail the plan.
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"undo\",\"steps\":0}]}", "invalid undo action: \"steps\" must be an integer 1..20");
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"redo\",\"steps\":21}]}", "invalid redo action: \"steps\" must be an integer 1..20");
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"undo\",\"steps\":\"2\"}]}", "invalid undo action: \"steps\" must be an integer 1..20");
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"undo\",\"all\":true}]}", "invalid undo action: unknown field \"all\"");
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"reset\",\"steps\":1}]}", "invalid reset action: unknown field \"steps\"");
}

test "parsePlan: formula enabled-form and empty-expr clears (§2, exactly one form)" {
    var p = try mustPlan(
        \\{"reply":"off","actions":[{"op":"formula","enabled":false},
        \\  {"op":"formula","enabled":true},{"op":"formula","axis":"x","expr":""},
        \\  {"op":"formula","axis":"y","expr":"  "}]}
    );
    defer p.deinit();
    try testing.expectEqual(@as(?bool, false), p.actions[0].formula.enabled);
    try testing.expectEqual(@as(?bool, true), p.actions[1].formula.enabled);
    try testing.expectEqual(@as(?bool, null), p.actions[2].formula.enabled);
    try testing.expectEqual(@as(u8, 'x'), p.actions[2].formula.axis);
    try testing.expectEqualStrings("", p.actions[2].formula.expr); // empty clears that axis
    try testing.expectEqualStrings("", p.actions[3].formula.expr); // blank ≡ empty

    // `enabled` rides ALONE; it must be a bool; the axis form still needs its axis.
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"formula\",\"enabled\":false,\"axis\":\"x\"}]}", "invalid formula action: \"enabled\" rides alone, without \"axis\"/\"expr\"");
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"formula\",\"enabled\":\"off\"}]}", "invalid formula action: \"enabled\" must be a boolean");
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"formula\",\"expr\":\"x*2\"}]}", "invalid formula action: \"axis\" must be \"x\" or \"y\"");
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"formula\",\"axis\":\"x\",\"expr\":7}]}", "invalid formula action: \"expr\" must be a string");
}

test "parsePlan: page custom cm dims (§2, exactly one of format / width+height)" {
    var p = try mustPlan("{\"reply\":\"ok\",\"actions\":[{\"op\":\"page\",\"width\":21,\"height\":29.7}]}");
    defer p.deinit();
    try testing.expectEqualStrings("", p.actions[0].page.format); // "" = the custom form
    try testing.expectEqual(@as(f64, 21), p.actions[0].page.width);
    try testing.expectEqual(@as(f64, 29.7), p.actions[0].page.height);

    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"page\",\"format\":\"a4\",\"width\":21,\"height\":29.7}]}", "invalid page action: exactly one of \"format\" / \"width\"+\"height\" is required");
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"page\",\"width\":21}]}", "invalid page action: custom dims need both \"width\" and \"height\"");
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"page\",\"width\":0.05,\"height\":10}]}", "invalid page action: \"width\"/\"height\" must be centimetres 0.1..500");
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"page\",\"width\":10,\"height\":501}]}", "invalid page action: \"width\"/\"height\" must be centimetres 0.1..500");
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"page\",\"width\":\"21\",\"height\":10}]}", "invalid page action: \"width\"/\"height\" must be centimetres 0.1..500");
    // The bare / bad-format failures keep their message.
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"page\"}]}", "invalid page action: \"format\" must be a lowercase ISO name a0\u{2013}a10, b0\u{2013}b10 or c0\u{2013}c10");
}

test "parsePlan: blank cm dims (§2 — both or neither, overriding format)" {
    var p = try mustPlan(
        \\{"reply":"ok","actions":[{"op":"blank","color":"#ffffff","width":10,"height":5},
        \\  {"op":"blank","color":"pink","format":"a4","width":10,"height":5}]}
    );
    defer p.deinit();
    try testing.expectEqual(@as(f64, 10), p.actions[0].blank.width);
    try testing.expectEqual(@as(f64, 5), p.actions[0].blank.height);
    try testing.expect(p.actions[0].blank.format == null);
    // Dims may ride beside a format — execution lets them override it (§2).
    try testing.expectEqualStrings("a4", p.actions[1].blank.format.?);
    try testing.expectEqual(@as(f64, 10), p.actions[1].blank.width);

    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"blank\",\"color\":\"#ffffff\",\"width\":10}]}", "invalid blank action: \"width\" and \"height\" ride together (both or neither)");
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"blank\",\"color\":\"#ffffff\",\"width\":10,\"height\":501}]}", "invalid blank action: \"width\"/\"height\" must be centimetres 0.1..500");
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"blank\",\"color\":\"#ffffff\",\"width\":true,\"height\":5}]}", "invalid blank action: \"width\"/\"height\" must be centimetres 0.1..500");
}

test "parsePlan: crop album spec key (console profile) — a bool riding the spec" {
    var p = try mustPlan("{\"reply\":\"ok\",\"actions\":[{\"op\":\"crop\",\"spec\":{\"x1\":\"10%\",\"album\":true}},{\"op\":\"crop\",\"spec\":{\"y2\":\"90%\",\"album\":false}}]}");
    defer p.deinit();
    try testing.expect(p.actions[0].crop.album);
    try testing.expectEqualStrings("10%", p.actions[0].crop.x1.?);
    try testing.expect(!p.actions[1].crop.album);

    // album is a modifier, not an edge: alone it does not satisfy the at-least-one rule.
    try mustReject(
        "{\"reply\":\"x\",\"actions\":[{\"op\":\"crop\",\"spec\":{\"album\":true}}]}",
        "invalid crop action: spec needs at least one of x1/x2/y1/y2/aspect",
    );
    try mustReject(
        "{\"reply\":\"x\",\"actions\":[{\"op\":\"crop\",\"spec\":{\"x1\":\"10%\",\"album\":\"yes\"}}]}",
        "invalid crop action: \"album\" must be a boolean",
    );
}

test "parsePlan: accent preset form (§10 — exactly one of color / preset)" {
    var p = try mustPlan("{\"reply\":\"ok\",\"actions\":[{\"op\":\"accent\",\"preset\":\" green \"},{\"op\":\"accent\",\"color\":\"#00ffff\"}]}");
    defer p.deinit();
    try testing.expectEqualStrings("green", p.actions[0].accent.preset); // trimmed
    try testing.expectEqualStrings("", p.actions[0].accent.color);
    try testing.expectEqualStrings("#00ffff", p.actions[1].accent.color);
    try testing.expectEqualStrings("", p.actions[1].accent.preset);

    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"accent\",\"color\":\"#00ffff\",\"preset\":\"green\"}]}", "invalid accent action: exactly one of \"color\" / \"preset\" is required");
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"accent\",\"preset\":\"  \"}]}", "invalid accent action: \"preset\" must be a non-empty string");
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"accent\",\"preset\":7}]}", "invalid accent action: \"preset\" must be a non-empty string");
}

test "parsePlan: reconnect and clear shapes (console profile §10)" {
    var p = try mustPlan("{\"reply\":\"ok\",\"actions\":[{\"op\":\"reconnect\",\"server\":\" a \"},{\"op\":\"clear\"}]}");
    defer p.deinit();
    try testing.expectEqualStrings("a", p.actions[0].reconnect.server); // trimmed
    try testing.expect(p.actions[1] == .clear);
    try testing.expect(!p.loadsWithoutTracing()); // a clear is a removal, not a load

    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"reconnect\",\"server\":\"\"}]}", "invalid reconnect action: \"server\" must be a non-empty string");
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"reconnect\"}]}", "invalid reconnect action: \"server\" must be a non-empty string");
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"reconnect\",\"server\":\"a\",\"token\":\"t\"}]}", "invalid reconnect action: unknown field \"token\"");
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"clear\",\"what\":\"image\"}]}", "invalid clear action: unknown field \"what\"");
}

test "parsePlan: clearChat — fieldless, top-level only (§10)" {
    var p = try mustPlan("{\"reply\":\"ok\",\"actions\":[{\"op\":\"clearChat\"}]}");
    defer p.deinit();
    try testing.expect(p.actions[0] == .clear_chat);
    try testing.expect(!p.loadsWithoutTracing()); // clearing chat loads nothing

    // Any field at all fails the plan; a variant carrying the op just loses the variant.
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"clearChat\",\"scope\":\"all\"}]}", "invalid clearChat action: unknown field \"scope\"");
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"clearChat\",\"confirm\":true}]}", "invalid clearChat action: unknown field \"confirm\"");
    // The persistence/consent toggles stay forbidden — never even unknown-op skips.
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"chatPersist\",\"on\":true}]}", "invalid plan: the \"chatPersist\" op is never model-drivable");
}

test "§13 forbidden ops: never registered, and a plan naming one is rejected outright" {
    for (op_registry) |d| try testing.expect(!isForbiddenOp(d.name));
    try testing.expect(isForbiddenOp("llm") and isForbiddenOp("paste") and isForbiddenOp("quit"));
    // Rejected with the §13 message — a hard tooth, never the unknown-op skip.
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"llm\",\"provider\":\"ollama\"}]}", "invalid plan: the \"llm\" op is never model-drivable");
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"paste\"}]}", "invalid plan: the \"paste\" op is never model-drivable");
    // …inside variants too.
    try mustReject("{\"reply\":\"x\",\"variants\":[{\"actions\":[{\"op\":\"exit\"}]}]}", "invalid plan: the \"exit\" op is never model-drivable");
}

test "parsePlan: openUrl takes an http(s) url + optional incognito; copy takes no fields (§10)" {
    var p = try mustPlan(
        \\{"reply":"ok","actions":[{"op":"openUrl","url":" https://a.example/cat.png ","incognito":true},
        \\  {"op":"openUrl","url":"HTTP://b.example/x.jpg"},{"op":"copy"}]}
    );
    defer p.deinit();
    try testing.expectEqual(@as(usize, 3), p.actions.len);
    try testing.expectEqualStrings("https://a.example/cat.png", p.actions[0].open_url.url); // trimmed
    try testing.expect(p.actions[0].open_url.incognito);
    try testing.expect(!p.actions[1].open_url.incognito); // absent = false
    try testing.expect(p.actions[2] == .copy);

    // Only http(s), no whitespace, url required; incognito must be a bool.
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"openUrl\",\"url\":\"ftp://a.example/x\"}]}", "invalid openUrl action: \"url\" must be an http(s) URL");
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"openUrl\",\"url\":\"https://a.example/a b\"}]}", "invalid openUrl action: \"url\" must be an http(s) URL");
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"openUrl\",\"url\":\"https://\"}]}", "invalid openUrl action: \"url\" must be an http(s) URL");
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"openUrl\"}]}", "invalid openUrl action: \"url\" must be an http(s) URL");
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"openUrl\",\"url\":\"https://a.example/x\",\"incognito\":\"yes\"}]}", "invalid openUrl action: \"incognito\" must be a boolean");
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"openUrl\",\"url\":\"https://a.example/x\",\"tab\":1}]}", "invalid openUrl action: unknown field \"tab\"");
    // copy is fieldless — anything else on it fails the plan.
    try mustReject("{\"reply\":\"x\",\"actions\":[{\"op\":\"copy\",\"name\":\"out\"}]}", "invalid copy action: unknown field \"name\"");
}

test "urlEchoedByUser: only the USER's own turns count, verbatim (§10)" {
    const url = "https://a.example/cat.png";
    const user_turn = [_]Turn{.{ .role = .user, .text = "load https://a.example/cat.png please" }};
    const assistant_turn = [_]Turn{.{ .role = .assistant, .text = "try https://a.example/cat.png" }};
    // The current turn or an earlier USER turn may carry the URL…
    try testing.expect(urlEchoedByUser(&.{}, "open https://a.example/cat.png", url));
    try testing.expect(urlEchoedByUser(&user_turn, "crop it", url));
    // …but assistant text never authorises one, and a rewritten/completed URL is not an echo.
    try testing.expect(!urlEchoedByUser(&assistant_turn, "crop it", url));
    try testing.expect(!urlEchoedByUser(&.{}, "load https://a.example/cat", url));
    try testing.expect(!urlEchoedByUser(&.{}, "", url));
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

test "parsePlan: the GUI editors' §10 ops stay unknown here — skipped with a warning" {
    var p = try mustPlan(
        \\{"reply":"ok","actions":[{"op":"theme","mode":"dark"},{"op":"units","value":"cm"},
        \\  {"op":"filter","mode":"bw"}]}
    );
    defer p.deinit();
    try testing.expectEqual(@as(usize, 1), p.actions.len); // only the filter survives
    try testing.expectEqual(@as(usize, 2), p.warnings.len);
    try testing.expectEqualStrings("Skipped unknown operation \"theme\"", p.warnings[0]);
    try testing.expectEqualStrings("Skipped unknown operation \"units\"", p.warnings[1]);
}

test "resolveServer: exact URL, else a UNIQUE host — never a model-introduced address (§10)" {
    const urls = [_][]const u8{ "http://alpha.example:8090", "https://beta.example", "http://alpha.example:9091" };

    // Exact URL match wins outright, even when the host is ambiguous.
    try testing.expectEqual(@as(usize, 0), resolveServer(&urls, "http://alpha.example:8090").index);
    // host:port and bare-host matches, case-insensitive, whitespace-trimmed.
    try testing.expectEqual(@as(usize, 2), resolveServer(&urls, "alpha.example:9091").index);
    try testing.expectEqual(@as(usize, 1), resolveServer(&urls, "BETA.example").index);
    try testing.expectEqual(@as(usize, 1), resolveServer(&urls, "  beta.example  ").index);
    // A bare host two entries share is ambiguous, not a guess.
    try testing.expect(resolveServer(&urls, "alpha.example") == .ambiguous);
    // Anything else — including an empty name — resolves to nothing.
    try testing.expect(resolveServer(&urls, "gamma.example") == .none);
    try testing.expect(resolveServer(&urls, "http://alpha.example:1234") == .none);
    try testing.expect(resolveServer(&urls, "") == .none);
    try testing.expect(resolveServer(&.{}, "alpha.example") == .none);
}

test "consoleContextAlloc: connections + active project + capped project names, tokens impossible" {
    const a = testing.allocator;

    // No connections: the context still stands, pointing at the console's own command.
    const empty = try consoleContextAlloc(a, &.{}, "");
    defer a.free(empty);
    try testing.expect(std.mem.indexOf(u8, empty, "Connections: none") != null);
    try testing.expect(std.mem.indexOf(u8, empty, "'/connect <url>'") != null);
    try testing.expect(std.mem.indexOf(u8, empty, "Active server project: none.") != null);

    // 22 project names on the active server: capped at 20 with a "+2 more".
    var many: [22][]const u8 = undefined;
    var bufs: [22][8]u8 = undefined;
    for (0..22) |i| many[i] = std.fmt.bufPrint(&bufs[i], "p{d}", .{i}) catch unreachable;
    const servers = [_]ConsoleServer{
        .{ .url = "http://a:8090", .active = true, .projects = &many },
        .{ .url = "http://b:9091", .projects = &.{} },
        .{ .url = "http://c:9092" }, // projects unknown (unreachable) → no listing line
    };
    const ctx = try consoleContextAlloc(a, &servers, "portrait");
    defer a.free(ctx);
    try testing.expect(std.mem.indexOf(u8, ctx, "Connections (3): http://a:8090 (active project's server), http://b:9091, http://c:9092.") != null);
    try testing.expect(std.mem.indexOf(u8, ctx, "Active server project: \"portrait\".") != null);
    try testing.expect(std.mem.indexOf(u8, ctx, "p19 (+2 more).") != null);
    try testing.expect(std.mem.indexOf(u8, ctx, "p20") == null); // beyond the cap
    try testing.expect(std.mem.indexOf(u8, ctx, "Projects on http://b:9091: (none)") != null);
    try testing.expect(std.mem.indexOf(u8, ctx, "Projects on http://c:9092") == null);
    // The suffix is built from URLs + names alone — ConsoleServer has no token field,
    // so nothing token-shaped can ever reach the prompt from here.
    try testing.expect(!@hasField(ConsoleServer, "token"));
}

test "openFile: a local path in a format we open, never a URL or an unknown type" {
    var ok = try mustPlan("{\"reply\":\"ok\",\"actions\":[{\"op\":\"openFile\",\"path\":\"~/Pictures/a.png\"}]}");
    defer ok.deinit();
    try testing.expectEqualStrings("~/Pictures/a.png", ok.actions[0].open_file.path);

    // Every format the console itself opens is allowed…
    inline for (.{ "a.jpg", "clip.mp4", "notes.json", "p.stencil", "/tmp/x.WEBP" }) |path| {
        var p = try mustPlan("{\"reply\":\"ok\",\"actions\":[{\"op\":\"openFile\",\"path\":\"" ++ path ++ "\"}]}");
        defer p.deinit();
        try testing.expectEqualStrings(path, p.actions[0].open_file.path);
    }
    // …and nothing else: no URL (openUrl owns those), no directory, no arbitrary file type.
    try mustReject(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"openFile\",\"path\":\"https://x.example/a.png\"}]}",
        "invalid openFile action: \"path\" must be a local file path",
    );
    try mustReject(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"openFile\",\"path\":\"~/Documents\"}]}",
        "invalid openFile action: \"~/Documents\" is not an image, video, .json layout or .stencil project",
    );
    try mustReject(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"openFile\",\"path\":\"~/.ssh/id_rsa\"}]}",
        "invalid openFile action: \"~/.ssh/id_rsa\" is not an image, video, .json layout or .stencil project",
    );
    try mustReject(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"openFile\",\"path\":\"\"}]}",
        "invalid openFile action: \"path\" must be a local file path",
    );
}

test "pathEchoedByUser: only a path the user wrote counts, wherever it was written" {
    const hist = [_]Turn{
        .{ .role = .user, .text = "load ~/Pictures/portrait.png please" },
        .{ .role = .assistant, .text = "sure — /etc/passwd is also readable" }, // never counts
    };
    try testing.expect(pathEchoedByUser(&hist, "crop it", "~/Pictures/portrait.png"));
    try testing.expect(pathEchoedByUser(&hist, "save into ~/Downloads", "~/Downloads"));
    try testing.expect(!pathEchoedByUser(&hist, "crop it", "/etc/passwd"));
    try testing.expect(!pathEchoedByUser(&hist, "crop it", "~/Pictures/other.png"));
}

test "pathEchoedByUser: naming a FOLDER grants the files inside it, but never a way out" {
    const hist = [_]Turn{.{ .role = .user, .text = "put the results in /Users/me/Downloads" }};
    // How people actually ask: the folder is theirs, the file name is the model's.
    try testing.expect(pathEchoedByUser(&hist, "go on", "/Users/me/Downloads/portrait-bw.png"));
    try testing.expect(pathEchoedByUser(&hist, "go on", "/Users/me/Downloads/sub/dir/x.stencil"));
    // A sibling folder was never given, and `..` voids the grant it would climb out of.
    try testing.expect(!pathEchoedByUser(&hist, "go on", "/Users/me/Documents/x.png"));
    try testing.expect(!pathEchoedByUser(&hist, "go on", "/Users/me/Downloads/../.ssh/id_rsa"));
    // A bare "/" grants nothing, however the model spells it.
    try testing.expect(!pathEchoedByUser(&[_]Turn{.{ .role = .user, .text = "save to /" }}, "", "/etc/hosts"));
}
