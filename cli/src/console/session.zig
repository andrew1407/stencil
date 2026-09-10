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

const max_states = 64; // pristine + up to 63 undoable edits; older edits drop off the front

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

    fn deinit(self: *EditState, gpa: std.mem.Allocator) void {
        if (self.filter_mode.len != 0) gpa.free(self.filter_mode);
        if (self.filter_color.len != 0) gpa.free(self.filter_color);
        if (self.lines_json.len != 0) gpa.free(self.lines_json);
        self.* = .{};
    }

    fn dupe(self: EditState, gpa: std.mem.Allocator) !EditState {
        var out = EditState{ .rotation = self.rotation, .crop = self.crop };
        errdefer out.deinit(gpa);
        out.filter_mode = try gpa.dupe(u8, self.filter_mode);
        out.filter_color = try gpa.dupe(u8, self.filter_color);
        out.lines_json = try gpa.dupe(u8, self.lines_json);
        return out;
    }

    fn lines(self: EditState) []const u8 {
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

    // ── Server connections (collaboration) ──
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

    // ── LLM assistant (/prompt, /llm) ──
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
    // ── /chat: opt-in per-project chat persistence (contract §12) ──
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

    /// True when a fetched server project is active (a target for sync / manual push).
    pub fn hasRemote(self: *const Session) bool {
        return self.remote_id != null and self.remote_url != null;
    }

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

    /// Drop every saved conversation turn (the `/chat clear` local half; §12).
    pub fn clearChat(self: *Session) void {
        for (self.chat_history.items) |t| self.gpa.free(t.text);
        self.chat_history.clearRetainingCapacity();
    }

    /// Append one conversation turn (owned copy of `text`), trimming the history to the
    /// most recent 32 turns (the §7/§12 bound) — the same pattern ask_options uses.
    pub fn appendChatTurn(self: *Session, role: llm.ChatRole, text: []const u8) !void {
        const dup = try self.gpa.dupe(u8, text);
        errdefer self.gpa.free(dup);
        try self.chat_history.append(self.gpa, .{ .role = role, .text = dup });
        while (self.chat_history.items.len > llm.max_chat_messages) {
            self.gpa.free(self.chat_history.items[0].text);
            _ = self.chat_history.orderedRemove(0);
        }
    }

    /// Replace the whole history with `turns` (a §12 restore), taking ownership of the
    /// slice and its texts (as returned by llm.parseChatDoc).
    pub fn adoptChatTurns(self: *Session, turns: []llm.Turn) void {
        self.clearChat();
        defer self.gpa.free(turns);
        self.chat_history.ensureTotalCapacity(self.gpa, turns.len) catch {
            for (turns) |t| self.gpa.free(t.text);
            return;
        };
        for (turns) |t| self.chat_history.appendAssumeCapacity(t);
    }

    /// Drop the pending `ask` card's options (contract §11) — called when a new card
    /// replaces it, when one is answered, and at teardown.
    pub fn clearAsk(self: *Session) void {
        for (self.ask_options) |o| self.gpa.free(o);
        if (self.ask_options.len != 0) self.gpa.free(self.ask_options);
        self.ask_options = &.{};
        self.ask_multi = false;
    }

    // ── §2.1 turn attachments ──
    /// Remember an uploaded image as an attachment of the current turn (taking ownership
    /// of `label_src`'s copy and `bytes`). A previous turn's list is dropped first, so
    /// `/upload a` `/upload b` `/prompt …` attaches exactly a and b; past
    /// `max_attachments` the oldest falls off, keeping the newest §7-many.
    pub fn addAttachment(self: *Session, label_src: []const u8, bytes: []u8, fmt: image.Format, temp: bool) !void {
        if (self.attachments_used) self.clearAttachments();
        const label = self.gpa.dupe(u8, label_src) catch |e| {
            self.gpa.free(bytes);
            return e;
        };
        self.attachments.append(self.gpa, .{ .label = label, .bytes = bytes, .fmt = fmt, .temp = temp }) catch |e| {
            self.gpa.free(label);
            self.gpa.free(bytes);
            return e;
        };
        if (self.attachments.items.len > llm.max_attachments) {
            var oldest = self.attachments.orderedRemove(0);
            oldest.deinit(self.gpa);
        }
    }

    /// Take back the newest attachment of the CURRENT turn, handing it to the caller (who
    /// deinits it). Null when the turn has none — including when a `/prompt` already spent
    /// them: that turn is over, so there is nothing left to take back.
    pub fn popAttachment(self: *Session) ?Attachment {
        if (self.attachments_used or self.attachments.items.len == 0) return null;
        return self.attachments.pop();
    }

    /// The images this turn will send, in attachment order (empty once a /prompt spent them).
    pub fn liveAttachments(self: *Session) []const Attachment {
        return if (self.attachments_used) &.{} else self.attachments.items;
    }

    /// Take back attachment `idx` (0-based) — `/unpaste <n>`. Caller deinits it.
    pub fn removeAttachment(self: *Session, idx: usize) ?Attachment {
        if (self.attachments_used or idx >= self.attachments.items.len) return null;
        return self.attachments.orderedRemove(idx);
    }

    /// The newest attachment of the current turn, borrowed — null under `popAttachment`'s rules.
    pub fn lastAttachment(self: *Session) ?*const Attachment {
        if (self.attachments_used or self.attachments.items.len == 0) return null;
        return &self.attachments.items[self.attachments.items.len - 1];
    }

    /// Mark the turn's attachments as spent (called once a /prompt turn has used them):
    /// the next `/upload` starts a new turn's list.
    pub fn consumeAttachments(self: *Session) void {
        if (self.attachments.items.len != 0) self.attachments_used = true;
    }

    pub fn clearAttachments(self: *Session) void {
        for (self.attachments.items) |*at| at.deinit(self.gpa);
        self.attachments.clearRetainingCapacity();
        self.attachments_used = false;
    }

    // ── images pasted into the line being typed ──
    /// The most images one line can hold markers for — the editor caps at its own (smaller)
    /// number; this is just the bound the fixed-size handoff buffers below are sized to.
    pub const max_pending = 8;

    /// Hold a pasted image against the line being edited (taking ownership of `bytes`).
    /// Nothing else in the session sees it until the line is submitted or abandoned.
    pub fn addPending(self: *Session, label_src: []const u8, bytes: []u8, fmt: image.Format, temp: bool) !void {
        const label = self.gpa.dupe(u8, label_src) catch |e| {
            self.gpa.free(bytes);
            return e;
        };
        self.pending.append(self.gpa, .{ .label = label, .bytes = bytes, .fmt = fmt, .temp = temp }) catch |e| {
            self.gpa.free(label);
            self.gpa.free(bytes);
            return e;
        };
    }

    /// Keep only the pending images at `kept` (0-based, in the order the line's markers now
    /// read) and drop the rest — how the editor reports a marker the user deleted or moved.
    pub fn keepPending(self: *Session, kept: []const usize) void {
        var out: [max_pending]Attachment = undefined;
        var taken = [_]bool{false} ** max_pending;
        var n: usize = 0;
        for (kept) |i| {
            if (i >= self.pending.items.len or i >= max_pending or taken[i] or n == out.len) continue;
            taken[i] = true;
            out[n] = self.pending.items[i];
            n += 1;
        }
        for (self.pending.items, 0..) |*at, i| {
            if (i < max_pending and taken[i]) continue; // moved into `out`, not ours to free
            at.deinit(self.gpa);
        }
        self.pending.clearRetainingCapacity();
        self.pending.appendSliceAssumeCapacity(out[0..n]); // n ≤ what we just cleared
    }

    pub fn clearPending(self: *Session) void {
        self.keepPending(&.{});
    }

    /// Hand the line's pending images over: the caller owns every one it receives (the
    /// session keeps none), which is how a submitted line turns them into uploads.
    pub fn takePending(self: *Session, out: []Attachment) usize {
        const n = @min(self.pending.items.len, out.len);
        @memcpy(out[0..n], self.pending.items[0..n]);
        for (self.pending.items[n..]) |*at| at.deinit(self.gpa); // more than `out` holds: dropped
        self.pending.clearRetainingCapacity();
        return n;
    }

    /// The session's LLM configuration, resolved from the captured environment on first use
    /// (so `/llm` overrides layer on top of the `STENCIL_LLM_*` initial values).
    pub fn llmConfig(self: *Session) !*llm.Config {
        if (self.llm_cfg == null) self.llm_cfg = try llm.Config.init(self.gpa, self.llm_env);
        return &self.llm_cfg.?;
    }

    // ── live events feed ──
    /// Open (or replace) the read-only project-events subscription to `client`'s server.
    /// Best-effort: a failed connect leaves the feed closed and is not fatal.
    pub fn openEvents(self: *Session, client: *server.Client) void {
        self.closeEvents();
        const conn = server.EditConn.open(self.gpa, client.io, client.base, client.token, "stencil-cli") catch return;
        const url = self.gpa.dupe(u8, client.base) catch {
            var c = conn;
            c.deinit();
            return;
        };
        self.events = conn;
        self.events_url = url;
    }

    pub fn closeEvents(self: *Session) void {
        if (self.events) |*e| e.deinit();
        self.events = null;
        if (self.events_url) |u| self.gpa.free(u);
        self.events_url = null;
    }

    // ── connection helpers ──
    /// Remember a successfully connected base URL in the known-servers pool (deduped).
    pub fn rememberServer(self: *Session, base: []const u8) !void {
        for (self.known_servers.items) |u| {
            if (std.mem.eql(u8, u, base)) return;
        }
        const dup = try self.gpa.dupe(u8, base);
        errdefer self.gpa.free(dup);
        try self.known_servers.append(self.gpa, dup);
    }

    pub fn findServer(self: *Session, url: []const u8) ?*server.Client {
        for (self.servers.items) |*c| {
            if (std.mem.eql(u8, c.base, url)) return c;
        }
        return null;
    }

    /// Index of a connected server by base URL, for in-place replacement (`/reconnect`).
    pub fn indexOfServer(self: *Session, url: []const u8) ?usize {
        for (self.servers.items, 0..) |*c, i| {
            if (std.mem.eql(u8, c.base, url)) return i;
        }
        return null;
    }

    pub fn dropServer(self: *Session, url: []const u8) bool {
        for (self.servers.items, 0..) |*c, i| {
            if (std.mem.eql(u8, c.base, url)) {
                if (self.events_url != null and std.mem.eql(u8, self.events_url.?, url)) self.closeEvents();
                c.deinit();
                _ = self.servers.orderedRemove(i);
                return true;
            }
        }
        return false;
    }

    /// Record the active remote project (owns copies of url + id).
    pub fn setRemote(self: *Session, url: []const u8, id: []const u8) !void {
        const u = try self.gpa.dupe(u8, url);
        errdefer self.gpa.free(u);
        const i = try self.gpa.dupe(u8, id);
        self.clearRemote();
        self.remote_url = u;
        self.remote_id = i;
    }

    /// Set the picked page format (canonical "A0".."C10" or "custom"), owned copy. It drives
    /// the header label, the layout `pageSize` written on save/sync, and the /blank default.
    pub fn setPageSize(self: *Session, name: []const u8) !void {
        const dup = try self.gpa.dupe(u8, name);
        if (self.page_size.len != 0) self.gpa.free(self.page_size);
        self.page_size = dup;
    }

    /// Replace the displayed label (e.g. after a rename), owned copy.
    pub fn setLabel(self: *Session, name: []const u8) !void {
        const dup = try self.gpa.dupe(u8, name);
        if (self.label) |l| self.gpa.free(l);
        self.label = dup;
    }

    /// Set the active project's custom name colour ("#rrggbb" or "" to clear), owned copy.
    pub fn setRemoteColor(self: *Session, color: []const u8) !void {
        const c = try self.gpa.dupe(u8, color);
        if (self.remote_color) |old| self.gpa.free(old);
        self.remote_color = c;
    }

    pub fn clearRemote(self: *Session) void {
        if (self.remote_url) |u| self.gpa.free(u);
        if (self.remote_id) |i| self.gpa.free(i);
        if (self.remote_color) |c| self.gpa.free(c);
        self.remote_url = null;
        self.remote_id = null;
        self.remote_color = null;
        self.remote_version = 0;
    }

    // ── image lifecycle ──
    pub fn hasImage(self: *Session) bool {
        return self.original != null;
    }

    /// The current derived view (valid whenever an image is loaded).
    pub fn current(self: *Session) *image.Rgba8 {
        return &self.working.?;
    }

    /// Number of history states (for the "[n/m]" position indicator).
    pub fn stateCount(self: *Session) usize {
        return self.history.items.len;
    }

    /// The current editing snapshot.
    pub fn state(self: *Session) EditState {
        return self.history.items[self.cursor];
    }

    /// Replace the whole session with a freshly loaded source: a pristine (un-rotated,
    /// un-cropped, un-filtered) state over `img` as the new original.
    /// `source_bytes` (optional, ownership transferred) are the raw encoded bytes of `img` in
    /// `fmt`; kept so a .stencil bundle embeds the untouched original. Pass null when none exist
    /// (blank / clipboard / a decoded peer image) and the bundle re-encodes from pixels.
    pub fn loadImage(self: *Session, img: image.Rgba8, label: []const u8, temp: bool, fmt: image.Format, source_bytes: ?[]u8) !void {
        const dup = self.gpa.dupe(u8, label) catch |e| {
            if (source_bytes) |b| self.gpa.free(b);
            return freeImg(self.gpa, img, e);
        };
        self.clearAll();
        self.history.append(self.gpa, .{}) catch |e| {
            self.gpa.free(dup);
            if (source_bytes) |b| self.gpa.free(b);
            return freeImg(self.gpa, img, e);
        };
        self.original = img;
        self.source_bytes = source_bytes;
        self.label = dup;
        self.temp = temp;
        self.default_fmt = fmt;
        self.cursor = 0;
        try self.rebuild();
    }

    /// Rebuild the derived view from the original + the current snapshot:
    /// rotate → crop → filter → rasterize lines. Replaces `working`.
    fn rebuild(self: *Session) !void {
        if (self.original == null) return;
        var img = try self.viewWithoutLines();
        errdefer img.deinit(self.gpa);
        rasterizeLinesJson(self.gpa, &img, self.history.items[self.cursor].lines());
        if (self.working) |*w| w.deinit(self.gpa);
        self.working = img;
    }

    /// The current view derived WITHOUT the drawn lines (rotate → crop → filter only) —
    /// the base `rebuild` rasterizes onto, and the base a variant render starts from so its
    /// own filter never recolours the lines. Caller owns the result; needs a loaded image.
    pub fn viewWithoutLines(self: *Session) !image.Rgba8 {
        const orig = self.original.?;
        var img = image.Rgba8{ .width = orig.width, .height = orig.height, .pixels = try self.gpa.dupe(u8, orig.pixels) };
        errdefer img.deinit(self.gpa);
        const st = self.history.items[self.cursor];
        if (@mod(st.rotation, 4) != 0) try pipeline.applyRotateBy(self.gpa, &img, st.rotation);
        if (st.crop) |cr| try pipeline.cropToRect(self.gpa, &img, cr);
        if (st.filter_mode.len != 0 and !std.ascii.eqlIgnoreCase(st.filter_mode, "none")) {
            const arg = if (std.ascii.eqlIgnoreCase(st.filter_mode, "custom")) st.filter_color else st.filter_mode;
            pipeline.applyFilterMode(self.gpa, &img, arg);
        }
        return img;
    }

    /// Push `next` as the new current state (dropping any redo states), then rebuild the view.
    /// Takes ownership of `next` only on a successful append; on append failure the caller's
    /// errdefer frees it. A rebuild failure is non-fatal (the old view simply remains).
    fn pushState(self: *Session, next: EditState) !void {
        self.dropAfterCursor();
        try self.history.append(self.gpa, next); // append fails BEFORE ownership → caller frees
        self.cursor = self.history.items.len - 1;
        while (self.history.items.len > max_states) {
            self.history.items[1].deinit(self.gpa);
            _ = self.history.orderedRemove(1);
            self.cursor -= 1;
        }
        self.rebuild() catch {};
    }

    // ── editing ops (each pushes a snapshot + rebuilds) ──

    /// Rotate by `n` quarter-turns (clockwise). The crop rect rides along into the new space.
    pub fn applyRotate(self: *Session, n: i32) !void {
        const cur = self.state();
        var next = try cur.dupe(self.gpa);
        errdefer next.deinit(self.gpa);
        if (next.crop) |cr| {
            const orig = self.original.?;
            const dims = core.rotatedDims(@intCast(orig.width), @intCast(orig.height), cur.rotation);
            next.crop = rotateRectQuarters(cr, dims.w, dims.h, n);
        }
        next.rotation = core.normalizeQuarters(cur.rotation + n);
        try self.pushState(next);
    }

    /// Crop to `rect` (given in CURRENT-view pixels); composes into rotated-original space.
    pub fn applyCrop(self: *Session, rect: core.Rect) !void {
        const cur = self.state();
        var next = try cur.dupe(self.gpa);
        errdefer next.deinit(self.gpa);
        // The view is rotate(original) cropped to `cur.crop`; a sub-rect maps back by its origin.
        const base_x: i32 = if (cur.crop) |c| c.x else 0;
        const base_y: i32 = if (cur.crop) |c| c.y else 0;
        const orig = self.original.?;
        const dims = core.rotatedDims(@intCast(orig.width), @intCast(orig.height), cur.rotation);
        next.crop = clampRect(.{ .x = base_x + rect.x, .y = base_y + rect.y, .w = rect.w, .h = rect.h }, dims.w, dims.h);
        try self.pushState(next);
    }

    /// Set the image filter (mode "none"|"bw"|"sepia"|"custom"; color is the custom hex).
    pub fn setFilter(self: *Session, mode: []const u8, color: []const u8) !void {
        const cur = self.state();
        var next = try cur.dupe(self.gpa);
        errdefer next.deinit(self.gpa);
        if (next.filter_mode.len != 0) self.gpa.free(next.filter_mode);
        if (next.filter_color.len != 0) self.gpa.free(next.filter_color);
        next.filter_mode = try self.gpa.dupe(u8, mode);
        next.filter_color = try self.gpa.dupe(u8, color);
        try self.pushState(next);
    }

    /// Append the lines from a layout JSON document to the drawing.
    pub fn addLines(self: *Session, layout_bytes: []const u8) !void {
        const add = try extractLinesJson(self.gpa, layout_bytes);
        defer self.gpa.free(add);
        const cur = self.state();
        var next = try cur.dupe(self.gpa);
        errdefer next.deinit(self.gpa);
        const merged = try mergeLinesJson(self.gpa, cur.lines(), add);
        if (next.lines_json.len != 0) self.gpa.free(next.lines_json);
        next.lines_json = merged;
        try self.pushState(next);
    }

    /// Replace the drawing's lines with those from a layout JSON document — the
    /// counterpart to `addLines` (which appends), for `apply <src> replace`.
    pub fn setLines(self: *Session, layout_bytes: []const u8) !void {
        const add = try extractLinesJson(self.gpa, layout_bytes);
        defer self.gpa.free(add);
        const cur = self.state();
        var next = try cur.dupe(self.gpa);
        errdefer next.deinit(self.gpa);
        const only = try self.gpa.dupe(u8, add);
        if (next.lines_json.len != 0) self.gpa.free(next.lines_json);
        next.lines_json = only;
        try self.pushState(next);
    }

    /// Adopt a server project's stored layout into the pristine state (used right after a
    /// fetch/pull loads the original), so the view shows the peer's crop/rotation/filter/lines.
    pub fn adoptServerLayout(self: *Session, layout_bytes: []const u8) !void {
        var st = EditState{};
        parseLayoutInto(self.gpa, layout_bytes, &st) catch {}; // partial parse still yields a valid st
        // Replace history[0] (we are right after loadImage, so cursor == 0). st is moved in.
        self.history.items[0].deinit(self.gpa);
        self.history.items[0] = st;
        self.cursor = 0;
        self.adoptLayoutMeta(layout_bytes); // page format + formulas (project-level, round-tripped)
        self.rebuild() catch {};
    }

    /// Parse the page format + x/y formulas out of a fetched layout (best-effort; cleared on miss).
    fn adoptLayoutMeta(self: *Session, layout_bytes: []const u8) void {
        self.clearFormat();
        var parsed = std.json.parseFromSlice(std.json.Value, self.gpa, layout_bytes, .{}) catch return;
        defer parsed.deinit();
        if (parsed.value != .object) return;
        const obj = parsed.value.object;
        if (jsonStr(obj, "pageSize")) |ps| self.page_size = self.gpa.dupe(u8, ps) catch &.{};
        self.custom_page_w = jsonNum(obj, "customPageWidth");
        self.custom_page_h = jsonNum(obj, "customPageHeight");
        if (obj.get("allowFormulas")) |v| {
            if (v == .bool) self.allow_formulas = v.bool;
        }
        if (jsonStr(obj, "formulaX")) |fx| self.formula_x = self.gpa.dupe(u8, fx) catch &.{};
        if (jsonStr(obj, "formulaY")) |fy| self.formula_y = self.gpa.dupe(u8, fy) catch &.{};
    }

    /// The project-level page format + formulas as a PageMeta view (borrows the owned slices).
    fn pageMeta(self: *Session) server.PageMeta {
        return .{
            .page_size = self.page_size,
            .custom_w = self.custom_page_w,
            .custom_h = self.custom_page_h,
            .allow_formulas = self.allow_formulas,
            .formula_x = self.formula_x,
            .formula_y = self.formula_y,
        };
    }

    /// Build the browser-compatible layout JSON for the current state (caller owns it).
    pub fn currentLayoutJson(self: *Session) ![]u8 {
        const st = self.state();
        const img = self.current();
        // No explicit crop means the WHOLE rotated original — state that instead of omitting
        // the field. The GUIs auto-crop a fresh image to the page aspect and only skip it when
        // the layout names a cropRect, so an omitted one makes them shrink the image on open
        // and strand lines drawn outside the page rect.
        const crop: ?server.CropRect = if (st.crop) |c|
            .{ .x = c.x, .y = c.y, .w = c.w, .h = c.h }
        else
            .{ .x = 0, .y = 0, .w = @intCast(img.width), .h = @intCast(img.height) };
        return server.buildLayout(self.gpa, @intCast(img.width), @intCast(img.height), st.lines(), st.filter_mode, st.filter_color, crop, st.rotation, self.pageMeta());
    }

    /// Page-format label shown next to the px size, e.g. "A4 21×29.7cm" (picked size oriented
    /// to the image, or "custom <w>×<h>cm"). Shares the one derivation with the one-shot
    /// pipeline's wrote line (page.pageLabelAlloc). Owned by the caller.
    pub fn pageFormatLabel(self: *Session) ![]u8 {
        const img = self.current();
        return pipeline.pageLabelAlloc(self.gpa, self.page_size, self.custom_page_w, self.custom_page_h, img.width, img.height);
    }

    pub fn undo(self: *Session) bool {
        if (self.cursor == 0) return false;
        self.cursor -= 1;
        self.rebuild() catch {};
        return true;
    }

    pub fn redo(self: *Session) bool {
        if (self.cursor + 1 >= self.history.items.len) return false;
        self.cursor += 1;
        self.rebuild() catch {};
        return true;
    }

    /// Revert to the pristine state, dropping every edit and the redo history.
    pub fn revert(self: *Session) void {
        self.cursor = 0;
        self.dropAfterCursor();
        self.rebuild() catch {};
    }

    fn dropAfterCursor(self: *Session) void {
        var i = self.history.items.len;
        while (i > self.cursor + 1) : (i -= 1) self.history.items[i - 1].deinit(self.gpa);
        self.history.shrinkRetainingCapacity(self.cursor + 1);
    }

    pub fn clearAll(self: *Session) void {
        for (self.history.items) |*st| st.deinit(self.gpa);
        self.history.clearRetainingCapacity();
        if (self.original) |*o| o.deinit(self.gpa);
        self.original = null;
        if (self.source_bytes) |b| self.gpa.free(b);
        self.source_bytes = null;
        if (self.working) |*w| w.deinit(self.gpa);
        self.working = null;
        if (self.prompt_b64) |b| self.gpa.free(b);
        self.prompt_b64 = null;
        self.prompt_digest = 0;
        self.cursor = 0;
        if (self.label) |l| self.gpa.free(l);
        self.label = null;
        self.temp = false;
        self.default_fmt = .png;
        self.clearFormat();
    }

    /// Set the x or y transform formula (validated via the shared parser; a non-empty
    /// expression enables formulas). Returns false on an invalid expression, state unchanged.
    pub fn setFormula(self: *Session, axis: u8, expr: []const u8) !bool {
        if (expr.len != 0 and !core.validateFormula(self.gpa, expr, axis)) return false;
        const dup = try self.gpa.dupe(u8, expr);
        const slot = if (axis == 'y') &self.formula_y else &self.formula_x;
        if (slot.len != 0) self.gpa.free(slot.*);
        slot.* = dup;
        if (expr.len != 0) self.allow_formulas = true;
        return true;
    }

    /// Toggle whether formulas apply on the saved layout (keeps the expressions).
    pub fn setAllowFormulas(self: *Session, on: bool) void {
        self.allow_formulas = on;
    }

    /// Clear both formula expressions and disable formulas (keeps the page format).
    pub fn clearFormulas(self: *Session) void {
        if (self.formula_x.len != 0) self.gpa.free(self.formula_x);
        if (self.formula_y.len != 0) self.gpa.free(self.formula_y);
        self.formula_x = &.{};
        self.formula_y = &.{};
        self.allow_formulas = false;
    }

    /// Reset the page format + formulas to "unset" (frees owned strings).
    pub fn clearFormat(self: *Session) void {
        self.clearFormulas();
        if (self.page_size.len != 0) self.gpa.free(self.page_size);
        self.page_size = &.{};
        self.custom_page_w = 0;
        self.custom_page_h = 0;
    }
};

fn freeImg(gpa: std.mem.Allocator, img: image.Rgba8, e: anyerror) anyerror {
    var m = img;
    m.deinit(gpa);
    return e;
}

/// Clamp a rect to lie within a `w`×`h` image (width/height ≥ 1).
fn clampRect(r: core.Rect, w: i32, h: i32) core.Rect {
    var out = r;
    out.w = std.math.clamp(r.w, 1, w);
    out.h = std.math.clamp(r.h, 1, h);
    out.x = std.math.clamp(r.x, 0, w - out.w);
    out.y = std.math.clamp(r.y, 0, h - out.h);
    return out;
}

/// Map a rect through `n` clockwise quarter-turns of its `w`×`h` containing image, returning
/// the rect in the rotated image's pixel space. Pure (axis-aligned 90° steps). Unit-tested.
fn rotateRectQuarters(rect: core.Rect, w: i32, h: i32, n: i32) core.Rect {
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
fn rasterizeLinesJson(gpa: std.mem.Allocator, img: *image.Rgba8, lines_json: []const u8) void {
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
fn extractLinesJson(gpa: std.mem.Allocator, layout_bytes: []const u8) ![]u8 {
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
fn mergeLinesJson(gpa: std.mem.Allocator, a: []const u8, b: []const u8) ![]u8 {
    const ai = innerArray(a);
    const bi = innerArray(b);
    if (ai.len == 0) return gpa.dupe(u8, if (bi.len == 0) "[]" else b);
    if (bi.len == 0) return gpa.dupe(u8, a);
    return std.fmt.allocPrint(gpa, "[{s},{s}]", .{ ai, bi });
}

/// The contents between the outermost `[` `]` of a JSON array string, trimmed (empty if none).
fn innerArray(s: []const u8) []const u8 {
    const t = std.mem.trim(u8, s, " \t\r\n");
    if (t.len < 2 or t[0] != '[' or t[t.len - 1] != ']') return "";
    return std.mem.trim(u8, t[1 .. t.len - 1], " \t\r\n");
}

/// Read a server layout document into an EditState (rotation, crop, filter, lines).
fn parseLayoutInto(gpa: std.mem.Allocator, layout_bytes: []const u8, out: *EditState) !void {
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

fn jsonStr(obj: std.json.ObjectMap, key: []const u8) ?[]const u8 {
    if (obj.get(key)) |v| {
        if (v == .string) return v.string;
    }
    return null;
}

fn jsonInt(obj: std.json.ObjectMap, key: []const u8) ?i64 {
    if (obj.get(key)) |v| {
        return switch (v) {
            .integer => |i| i,
            .float => |f| @intFromFloat(f),
            else => null,
        };
    }
    return null;
}

fn jsonNum(obj: std.json.ObjectMap, key: []const u8) f64 {
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
