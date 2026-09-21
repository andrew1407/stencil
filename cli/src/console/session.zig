//! Console working-image state. Structured (browser-compatible) model: an untouched ORIGINAL image
//! plus a stack of `EditState` snapshots (rotation + crop + filter + lines). The current view is
//! DERIVED on demand — rotate → crop → filter → rasterize lines — so the same state serializes as a
//! layout the browser/desktop editors render, making every CLI edit sync to peers rather than baked
//! into a `result`. `/undo`, `/redo`, `/reset` move a cursor over the snapshots and rebuild the view.
const std = @import("std");
const image = @import("../media/image.zig");
const server = @import("../server/client.zig");
const core = @import("../core.zig");
const pipeline = @import("../pipeline.zig");
const layout_mod = @import("../media/layout.zig");
const llm = @import("../llm.zig");
const derivedView = @import("render/derivedView.zig");
const geom = @import("session/geom.zig");
const layoutJson = @import("session/layoutJson.zig");
const state_mod = @import("session/state.zig");

pub const max_states = state_mod.max_states;
pub const EditState = state_mod.EditState;
pub const Attachment = state_mod.Attachment;

pub const freeImg = geom.freeImg;
pub const clampRect = geom.clampRect;
pub const rotateRectQuarters = geom.rotateRectQuarters;

pub const rasterizeLinesJson = layoutJson.rasterizeLinesJson;
pub const extractLinesJson = layoutJson.extractLinesJson;
pub const mergeLinesJson = layoutJson.mergeLinesJson;
pub const innerArray = layoutJson.innerArray;
pub const parseLayoutInto = layoutJson.parseLayoutInto;
pub const jsonStr = layoutJson.jsonStr;
pub const jsonInt = layoutJson.jsonInt;
pub const jsonNum = layoutJson.jsonNum;

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
    // Every base URL this session successfully /connect-ed to (owned; survives a /disconnect). The ONLY
    // pool a plan `connect` op may resolve against — the assistant can never introduce a server.
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
    // The option labels of the LAST `ask` card the assistant printed (§11), owned: they let the next
    // /prompt answer by number ("2", "1,3"). Cleared once used, or when a later turn asks something else.
    ask_options: [][]u8 = &.{},
    ask_multi: bool = false,
    // /prompt attachment cache: the base64 PNG of the working image keyed by a digest of its pixels,
    // so a no-edit follow-up turn re-sends without paying a full PNG + base64 re-encode.
    prompt_b64: ?[]u8 = null,
    prompt_digest: u64 = 0,
    // §2.1 multi-image plans: the images `/upload`ed for the CURRENT turn, in upload order — what an
    // `image` op indexes (1-based). A /prompt consumes the list, so the next upload starts a fresh turn.
    attachments: std.ArrayList(Attachment) = .empty,
    attachments_used: bool = false,
    // Images pasted into the LINE being typed (Ctrl-V, or a pasted image-file path), held against the
    // `[Image #N …]` markers until Enter turns them into real uploads. An abandoned line drops them.
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
    // Ctrl-C during a long call: the console installs its tty watch here so a waiting call can be
    // cancelled. Null in the one-shot CLI and in tests, where calls simply run to completion.
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

test {
    _ = @import("session/attachments.zig");
    _ = @import("session/chat.zig");
    _ = @import("session/servers.zig");
    _ = @import("session/history.zig");
    _ = @import("session/edits.zig");
    _ = geom;
    _ = layoutJson;
    _ = state_mod;
}
