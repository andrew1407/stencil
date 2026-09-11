//! Console working-image state. Structured (browser-compatible) model: an untouched
//! ORIGINAL image plus a stack of `EditState` snapshots (rotation + crop + filter + lines).
//! The current view is DERIVED on demand — rotate → crop → filter → rasterize lines — so the
//! exact same state can be serialized as a layout the browser/desktop editors render, making
//! every CLI edit (crop/rotate/filter/draw) sync to peers, not just baked into a `result`.
//! `/undo`, `/redo`, `/reset` move a cursor over the snapshots and rebuild the view.
const std = @import("std");
const image = @import("../image.zig");
const server = @import("../serverClient.zig");
const core = @import("../core.zig");
const pipeline = @import("../pipeline.zig");
const layout_mod = @import("../layout.zig");
const llm = @import("../llm.zig");
const derivedView = @import("derivedView.zig");

pub const max_states = 64; // pristine + up to 63 undoable edits; older edits drop off the front

/// One editing snapshot, mirroring the browser layout: a rotation (0..3 clockwise quarters,
/// applied to the original FIRST), a crop rect in rotated-original pixels, an image filter
/// (mode "none"|"bw"|"sepia"|"custom" + custom hex color), and the drawn lines as a JSON array
/// string (browser line schema). All owned. Empty `lines_json` means "[]".
pub const EditState = struct {
    rotation: i32 = 0,
    crop: ?core.Rect = null,
    filter_mode: []u8 = &.{},
    filter_color: []u8 = &.{},
    lines_json: []u8 = &.{},

    pub fn deinit(self: *EditState, gpa: std.mem.Allocator) void {
        if (self.filter_mode.len != 0) gpa.free(self.filter_mode);
        if (self.filter_color.len != 0) gpa.free(self.filter_color);
        if (self.lines_json.len != 0) gpa.free(self.lines_json);
        self.* = .{};
    }

    pub fn dupe(self: EditState, gpa: std.mem.Allocator) !EditState {
        var out = EditState{ .rotation = self.rotation, .crop = self.crop };
        errdefer out.deinit(gpa);
        out.filter_mode = try gpa.dupe(u8, self.filter_mode);
        out.filter_color = try gpa.dupe(u8, self.filter_color);
        out.lines_json = try gpa.dupe(u8, self.lines_json);
        return out;
    }

    pub fn lines(self: EditState) []const u8 {
        return if (self.lines_json.len == 0) "[]" else self.lines_json;
    }
};

/// One image the user brought into the turn with `/upload` (contract §2.1/§7): its
/// label (the path/URL it came from), the raw ENCODED bytes — kept instead of pixels so
/// a whole turn of attachments costs kilobytes, and so a `save` embeds the untouched
/// original — plus how to re-encode it. All owned by the session.
pub const Attachment = struct {
    label: []u8,
    bytes: []u8,
    fmt: image.Format = .png,
    temp: bool = false, // came from a URL/in-memory source, not a file on disk

    pub fn deinit(self: *Attachment, gpa: std.mem.Allocator) void {
        gpa.free(self.label);
        gpa.free(self.bytes);
        self.* = undefined;
    }
};

pub const Session = struct {
    gpa: std.mem.Allocator,
    label: ?[]u8 = null, // owned display label (the source path / URL / "blank" / "clipboard")
    temp: bool = false, // in-memory only (URL, blank, clipboard), not backed by a file on disk
    default_fmt: image.Format = .png,
    original: ?image.Rgba8 = null, // the untouched base image (owned); every view derives from it
    source_bytes: ?[]u8 = null, // raw encoded bytes of `original` (owned); embedded verbatim in a .stencil (null ⇒ re-encode)
    history: std.ArrayList(EditState) = .empty, // [0] = pristine; the current state is history[cursor]
    cursor: usize = 0,
    working: ?image.Rgba8 = null, // the derived current view (owned), rebuilt on every change
    base: derivedView.Base = .{}, // the cached rotate→crop→filter view the lines are drawn onto

    // Server connections (collaboration)
    servers: std.ArrayList(server.Client) = .empty, // connected servers (REST clients)
    // Every base URL this session successfully /connect-ed to (owned; survives a
    // /disconnect). The ONLY pool a plan `connect` op may resolve against — the
    // assistant can reconnect a server the user named, never introduce one.
    known_servers: std.ArrayList([]const u8) = .empty,
    sync: bool = false, // when on, edits auto-upload the layout + result to the active remote
    dirty: bool = false, // a pending sync upload coalesced from a burst of edits (see remoteEvents.flushSync)
    remote_url: ?[]u8 = null, // owned base URL of the active fetched project's server
    remote_id: ?[]u8 = null, // owned id of the active fetched project
    remote_version: i64 = 0, // last server version we hold for the active project (LWW guard for auto-pull)
    remote_color: ?[]u8 = null, // owned active project's custom name colour ("#rrggbb"); null/"" = default
    events: ?server.EditConn = null, // live read-only project-events feed (opened while syncing)
    events_url: ?[]u8 = null, // owned base URL the events feed is connected to

    // LLM assistant (/prompt, /llm)
    llm_env: llm.Env = .{}, // the raw STENCIL_LLM_* values captured at startup
    llm_cfg: ?llm.Config = null, // resolved lazily on first /prompt or /llm (in-session overrides)
    // The option labels of the LAST `ask` card the assistant printed (contract §11), owned.
    // They let the NEXT /prompt be answered by number ("2", "1,3") instead of retyping a
    // label; cleared once used, or when a later turn asks something else.
    ask_options: [][]u8 = &.{},
    ask_multi: bool = false,
    // /prompt attachment cache: the base64 PNG of the working image, keyed by a digest of
    // its pixels (owned) — a no-edit follow-up turn (e.g. answering an ask card) re-sends
    // the same image without paying a full PNG + base64 re-encode.
    prompt_b64: ?[]u8 = null,
    prompt_digest: u64 = 0,
    // §2.1 multi-image plans: the images `/upload`ed for the CURRENT turn, in upload
    // order — what an `image` op indexes (1-based) and what the next /prompt attaches
    // when there is more than one. A /prompt consumes the list: the next /upload after
    // it starts a fresh turn rather than piling onto the answered one.
    attachments: std.ArrayList(Attachment) = .empty,
    attachments_used: bool = false,
    // Images pasted into the LINE being typed (Ctrl-V, or a pasted image-file path): held
    // against the `[Image #N …]` markers the editor shows in the prompt until Enter turns
    // them into real uploads (attachments.drainPending). An abandoned line drops them.
    pending: std.ArrayList(Attachment) = .empty,
    // /chat: opt-in per-project chat persistence (contract §12)
    chat_on: bool = false, // default OFF — replaying/persisting chat is an explicit opt-in
    chat_history: std.ArrayList(llm.Turn) = .empty, // owned texts; ≤ llm.max_chat_messages turns
    // §10 clearChat: armed by the plan action, consumed once the /prompt turn settles.
    pending_chat_clear: bool = false,
    // How the deferred clearChat confirm asks its question: the console wires the TTY
    // editor's keypress confirm (or a piped-stdin line read); null declines, like an EOF.
    confirm_fn: ?*const fn (ctx: ?*anyopaque, question: []const u8) bool = null,
    confirm_ctx: ?*anyopaque = null,
    // Ctrl-C during a long call (an LLM turn): the console installs its tty watch here so a
    // waiting call can be cancelled. Null in the one-shot CLI and in tests — nothing to poll,
    // so calls simply run to completion.
    cancel_ctx: ?*anyopaque = null,
    cancel_poll: ?*const fn (ctx: *anyopaque, timeout_ms: i32) bool = null,

    // Page format + x/y formulas, set via /format or round-tripped through a fetched layout.
    page_size: []u8 = &.{}, // "" | a named format ("A0".."C10") | "custom"
    custom_page_w: f64 = 0, // cm; 0 = unset
    custom_page_h: f64 = 0,
    allow_formulas: bool = false,
    formula_x: []u8 = &.{}, // "" = identity transform
    formula_y: []u8 = &.{},
    pub const hasRemote = @import("session/servers.zig").hasRemote;

    /// True when the user pressed Ctrl-C since the last check — waits up to `timeout_ms` for
    /// one, so a caller waiting on a slow call can use this as its whole idle beat.
    pub fn cancelRequested(self: *Session, timeout_ms: i32) bool {
        const poll = self.cancel_poll orelse return false;
        const ctx = self.cancel_ctx orelse return false;
        return poll(ctx, timeout_ms);
    }

    pub fn deinit(self: *Session) void {
        self.clearAll();
        self.closeEvents();
        self.history.deinit(self.gpa);
        for (self.servers.items) |*c| c.deinit();
        self.servers.deinit(self.gpa);
        for (self.known_servers.items) |u| self.gpa.free(u);
        self.known_servers.deinit(self.gpa);
        self.clearRemote();
        if (self.llm_cfg) |*c| c.deinit(self.gpa);
        self.llm_cfg = null;
        self.clearAsk();
        self.clearChat();
        self.chat_history.deinit(self.gpa);
        self.clearAttachments();
        self.attachments.deinit(self.gpa);
        self.clearPending();
        self.pending.deinit(self.gpa);
    }
    pub const clearChat = @import("session/chat.zig").clearChat;
    pub const appendChatTurn = @import("session/chat.zig").appendChatTurn;
    pub const adoptChatTurns = @import("session/chat.zig").adoptChatTurns;
    pub const clearAsk = @import("session/chat.zig").clearAsk;
    pub const addAttachment = @import("session/attachments.zig").addAttachment;
    pub const popAttachment = @import("session/attachments.zig").popAttachment;
    pub const liveAttachments = @import("session/attachments.zig").liveAttachments;
    pub const removeAttachment = @import("session/attachments.zig").removeAttachment;
    pub const lastAttachment = @import("session/attachments.zig").lastAttachment;
    pub const consumeAttachments = @import("session/attachments.zig").consumeAttachments;
    pub const clearAttachments = @import("session/attachments.zig").clearAttachments;

    /// The most images one line can hold markers for — the editor caps at its own (smaller)
    /// number; this is just the bound the fixed-size handoff buffers below are sized to.
    pub const max_pending = 8;
    pub const addPending = @import("session/attachments.zig").addPending;
    pub const keepPending = @import("session/attachments.zig").keepPending;
    pub const clearPending = @import("session/attachments.zig").clearPending;
    pub const takePending = @import("session/attachments.zig").takePending;
    pub const llmConfig = @import("session/servers.zig").llmConfig;
    pub const openEvents = @import("session/servers.zig").openEvents;
    pub const closeEvents = @import("session/servers.zig").closeEvents;
    pub const rememberServer = @import("session/servers.zig").rememberServer;
    pub const findServer = @import("session/servers.zig").findServer;
    pub const indexOfServer = @import("session/servers.zig").indexOfServer;
    pub const dropServer = @import("session/servers.zig").dropServer;
    pub const setRemote = @import("session/servers.zig").setRemote;
    pub const setPageSize = @import("session/edits.zig").setPageSize;
    pub const setLabel = @import("session/edits.zig").setLabel;
    pub const setRemoteColor = @import("session/servers.zig").setRemoteColor;
    pub const clearRemote = @import("session/servers.zig").clearRemote;
    pub const hasImage = @import("session/history.zig").hasImage;
    pub const current = @import("session/history.zig").current;
    pub const stateCount = @import("session/history.zig").stateCount;
    pub const state = @import("session/history.zig").state;
    pub const loadImage = @import("session/history.zig").loadImage;
    pub const rebuild = @import("session/history.zig").rebuild;
    pub const viewWithoutLines = @import("session/history.zig").viewWithoutLines;
    pub const pushState = @import("session/history.zig").pushState;
    pub const applyRotate = @import("session/edits.zig").applyRotate;
    pub const applyCrop = @import("session/edits.zig").applyCrop;
    pub const setFilter = @import("session/edits.zig").setFilter;
    pub const addLines = @import("session/edits.zig").addLines;
    pub const setLines = @import("session/edits.zig").setLines;
    pub const adoptServerLayout = @import("session/edits.zig").adoptServerLayout;
    pub const adoptLayoutMeta = @import("session/edits.zig").adoptLayoutMeta;
    pub const pageMeta = @import("session/edits.zig").pageMeta;
    pub const currentLayoutJson = @import("session/edits.zig").currentLayoutJson;
    pub const pageFormatLabel = @import("session/edits.zig").pageFormatLabel;
    pub const undo = @import("session/history.zig").undo;
    pub const redo = @import("session/history.zig").redo;
    pub const revert = @import("session/history.zig").revert;
    pub const dropAfterCursor = @import("session/history.zig").dropAfterCursor;
    pub const clearAll = @import("session/history.zig").clearAll;
    pub const setFormula = @import("session/edits.zig").setFormula;
    pub const setAllowFormulas = @import("session/edits.zig").setAllowFormulas;
    pub const clearFormulas = @import("session/edits.zig").clearFormulas;
    pub const clearFormat = @import("session/edits.zig").clearFormat;
};

pub fn freeImg(gpa: std.mem.Allocator, img: image.Rgba8, e: anyerror) anyerror {
    var m = img;
    m.deinit(gpa);
    return e;
}

/// Clamp a rect to lie within a `w`×`h` image (width/height ≥ 1).
pub fn clampRect(r: core.Rect, w: i32, h: i32) core.Rect {
    var out = r;
    out.w = std.math.clamp(r.w, 1, w);
    out.h = std.math.clamp(r.h, 1, h);
    out.x = std.math.clamp(r.x, 0, w - out.w);
    out.y = std.math.clamp(r.y, 0, h - out.h);
    return out;
}

/// Map a rect through `n` clockwise quarter-turns of its `w`×`h` containing image, returning
/// the rect in the rotated image's pixel space. Pure (axis-aligned 90° steps). Unit-tested.
pub fn rotateRectQuarters(rect: core.Rect, w: i32, h: i32, n: i32) core.Rect {
    var r = rect;
    var cw = w;
    var ch = h;
    var q = core.normalizeQuarters(n);
    while (q > 0) : (q -= 1) {
        // One clockwise step: new dims (ch, cw); (x,y) → (ch - y - rh, x). Compute into a
        // temp first — assigning a struct literal that reads `r` would alias the in-place write.
        const nr = core.Rect{ .x = ch - r.y - r.h, .y = r.x, .w = r.h, .h = r.w };
        r = nr;
        const t = cw;
        cw = ch;
        ch = t;
    }
    return r;
}

/// Rasterize the lines in a JSON array string onto `img` (best-effort; bad JSON draws nothing).
pub fn rasterizeLinesJson(gpa: std.mem.Allocator, img: *image.Rgba8, lines_json: []const u8) void {
    const wrapped = std.fmt.allocPrint(gpa, "{{\"lines\":{s}}}", .{lines_json}) catch return;
    defer gpa.free(wrapped);
    var parsed = layout_mod.parse(gpa, wrapped) catch return;
    defer parsed.deinit();
    for (parsed.lines) |line| {
        core.rasterizeLine(img.pixels, @intCast(img.width), @intCast(img.height), line);
    }
}

/// Extract the `lines` array of a layout JSON document as an owned JSON array string ("[]" if
/// absent). Caller owns the result.
pub fn extractLinesJson(gpa: std.mem.Allocator, layout_bytes: []const u8) ![]u8 {
    var parsed = std.json.parseFromSlice(std.json.Value, gpa, layout_bytes, .{}) catch return gpa.dupe(u8, "[]");
    defer parsed.deinit();
    if (parsed.value == .object) {
        if (parsed.value.object.get("lines")) |lv| {
            if (lv == .array) return std.json.Stringify.valueAlloc(gpa, lv, .{});
        }
    }
    return gpa.dupe(u8, "[]");
}

/// Concatenate two JSON array strings ("[...]") into one. Pure string work. Caller owns it.
pub fn mergeLinesJson(gpa: std.mem.Allocator, a: []const u8, b: []const u8) ![]u8 {
    const ai = innerArray(a);
    const bi = innerArray(b);
    if (ai.len == 0) return gpa.dupe(u8, if (bi.len == 0) "[]" else b);
    if (bi.len == 0) return gpa.dupe(u8, a);
    return std.fmt.allocPrint(gpa, "[{s},{s}]", .{ ai, bi });
}

/// The contents between the outermost `[` `]` of a JSON array string, trimmed (empty if none).
pub fn innerArray(s: []const u8) []const u8 {
    const t = std.mem.trim(u8, s, " \t\r\n");
    if (t.len < 2 or t[0] != '[' or t[t.len - 1] != ']') return "";
    return std.mem.trim(u8, t[1 .. t.len - 1], " \t\r\n");
}

/// Read a server layout document into an EditState (rotation, crop, filter, lines).
pub fn parseLayoutInto(gpa: std.mem.Allocator, layout_bytes: []const u8, out: *EditState) !void {
    out.lines_json = try extractLinesJson(gpa, layout_bytes);
    var parsed = std.json.parseFromSlice(std.json.Value, gpa, layout_bytes, .{}) catch return;
    defer parsed.deinit();
    if (parsed.value != .object) return;
    const obj = parsed.value.object;
    if (jsonStr(obj, "imageFilter")) |m| out.filter_mode = try gpa.dupe(u8, m);
    if (jsonStr(obj, "filterColor")) |c| out.filter_color = try gpa.dupe(u8, c);
    if (jsonInt(obj, "rotationQuarters")) |r| out.rotation = core.normalizeQuarters(@intCast(r));
    if (obj.get("cropRect")) |cv| {
        if (cv == .object) {
            const co = cv.object;
            out.crop = .{
                .x = @intFromFloat(jsonNum(co, "x")),
                .y = @intFromFloat(jsonNum(co, "y")),
                .w = @intFromFloat(jsonNum(co, "width")),
                .h = @intFromFloat(jsonNum(co, "height")),
            };
        }
    }
}

pub fn jsonStr(obj: std.json.ObjectMap, key: []const u8) ?[]const u8 {
    if (obj.get(key)) |v| {
        if (v == .string) return v.string;
    }
    return null;
}

pub fn jsonInt(obj: std.json.ObjectMap, key: []const u8) ?i64 {
    if (obj.get(key)) |v| {
        return switch (v) {
            .integer => |i| i,
            .float => |f| @intFromFloat(f),
            else => null,
        };
    }
    return null;
}

pub fn jsonNum(obj: std.json.ObjectMap, key: []const u8) f64 {
    if (obj.get(key)) |v| {
        return switch (v) {
            .integer => |i| @floatFromInt(i),
            .float => |f| f,
            else => 0,
        };
    }
    return 0;
}

const testing = std.testing;

test "rotateRectQuarters maps a rect through clockwise quarter-turns" {
    // A 10x4 rect at (1,2) in a 100x50 image, rotated once clockwise → 50x100 image.
    const r1 = rotateRectQuarters(.{ .x = 1, .y = 2, .w = 10, .h = 4 }, 100, 50, 1);
    // new x = ch - y - rh = 50 - 2 - 4 = 44; new y = x = 1; w=rh=4; h=rw=10.
    try testing.expectEqual(@as(i32, 44), r1.x);
    try testing.expectEqual(@as(i32, 1), r1.y);
    try testing.expectEqual(@as(i32, 4), r1.w);
    try testing.expectEqual(@as(i32, 10), r1.h);
    // Four turns returns to the original.
    const r4 = rotateRectQuarters(.{ .x = 1, .y = 2, .w = 10, .h = 4 }, 100, 50, 4);
    try testing.expectEqual(@as(i32, 1), r4.x);
    try testing.expectEqual(@as(i32, 2), r4.y);
    try testing.expectEqual(@as(i32, 10), r4.w);
    try testing.expectEqual(@as(i32, 4), r4.h);
}

test "mergeLinesJson concatenates arrays, handles empties" {
    const a = testing.allocator;
    const m1 = try mergeLinesJson(a, "[{\"a\":1}]", "[{\"b\":2}]");
    defer a.free(m1);
    try testing.expectEqualStrings("[{\"a\":1},{\"b\":2}]", m1);
    const m2 = try mergeLinesJson(a, "[]", "[{\"b\":2}]");
    defer a.free(m2);
    try testing.expectEqualStrings("[{\"b\":2}]", m2);
    const m3 = try mergeLinesJson(a, "[{\"a\":1}]", "[]");
    defer a.free(m3);
    try testing.expectEqualStrings("[{\"a\":1}]", m3);
}

test "extractLinesJson pulls the lines array, defaults to []" {
    const a = testing.allocator;
    const l = try extractLinesJson(a, "{\"lines\":[{\"color\":\"#f00\"}],\"imageFilter\":\"bw\"}");
    defer a.free(l);
    try testing.expectEqualStrings("[{\"color\":\"#f00\"}]", l);
    const none = try extractLinesJson(a, "{\"imageFilter\":\"bw\"}");
    defer a.free(none);
    try testing.expectEqualStrings("[]", none);
}

test {
    _ = @import("session/attachments.zig");
    _ = @import("session/chat.zig");
    _ = @import("session/servers.zig");
    _ = @import("session/history.zig");
    _ = @import("session/edits.zig");
}
