//! The console's LLM assistant (/prompt, /llm) — the executor half of the CLI's
//! llm-contract.md port: provider config, prompt assembly + attachments, op-plan
//! execution through the normal session operations, and variant rendering.
const std = @import("std");
const image = @import("../image.zig");
const pipeline = @import("../pipeline.zig");
const server = @import("../serverClient.zig");
const logo = @import("../logo.zig");
const core = @import("../core.zig");
const commands = @import("commands.zig");
const llm = @import("../llm.zig");
const layout_mod = @import("../layout.zig");
const project = @import("../project.zig");
const ui = @import("ui.zig");
const Session = @import("session.zig").Session;
const Attachment = @import("session.zig").Attachment;
const handlers = @import("handlers.zig");
const attachments = @import("attachments.zig");
const remoteEvents = @import("remoteEvents.zig");

// ── LLM assistant (/prompt, /llm) ──────────────────────────────────────────────
//
// The wire/parse half lives in llm.zig (the CLI's port of llm-contract.md); this is the
// executor half: validated op-plan actions run through the SAME session operations the
// console commands use, variants through the same pipeline blocks the view rebuild uses.

/// `/llm [provider|url|model|key|server <value>]` — show (bare) or override the session's
/// LLM config, seeded from `STENCIL_LLM_*`; secrets are masked. A provider change re-fills
/// its default baseUrl unless the URL was overridden this session via '/llm url'.
pub fn doLlm(session: *Session, arg: []const u8) !void {
    const cfg = try session.llmConfig();
    switch (llm.parseCmd(arg)) {
        .show => showLlm(session, cfg),
        .usage => logo.print("usage: /llm [provider <ollama|openai-compat|stencil-server> | url <baseUrl> | model <name> | key <apiKey> | server <serverUrl>]\n", .{}),
        .bad_provider => |t| logo.err("unknown LLM provider '{s}' — ollama, openai-compat, or stencil-server\n", .{t}),
        .provider => |p| {
            try cfg.setProvider(session.gpa, p);
            if (p == .stencil_server) {
                logo.print("llm provider set to {s}\n", .{p.label()});
            } else {
                logo.print("llm provider set to {s} (url {s})\n", .{ p.label(), cfg.base_url });
            }
        },
        .url => |u| {
            try cfg.setBaseUrl(session.gpa, u);
            logo.print("llm url set to {s}\n", .{cfg.base_url});
        },
        .model => |m| {
            try cfg.setModel(session.gpa, m);
            logo.print("llm model set to {s}\n", .{cfg.model});
        },
        .key => |k| {
            try cfg.setApiKey(session.gpa, k);
            logo.print("llm api key set (hidden)\n", .{});
        },
        .server => |s| {
            try cfg.setServerUrl(session.gpa, s);
            logo.print("llm server set to {s}\n", .{cfg.server_url});
        },
    }
}

/// The bare `/llm` listing: every setting on its own row, secrets masked, plus where the
/// stencil-server auth would come from.
fn showLlm(session: *Session, cfg: *const llm.Config) void {
    logo.print("llm: provider {s} — '/llm provider|url|model|key|server <value>' to change (env: STENCIL_LLM_*)\n", .{cfg.provider.label()});
    logo.print("  url:    {s}\n", .{if (cfg.base_url.len != 0) cfg.base_url else "(none)"});
    logo.print("  model:  {s}\n", .{if (cfg.model.len != 0) cfg.model else "(provider default)"});
    logo.print("  key:    {s}\n", .{if (cfg.api_key.len != 0) "(set, hidden)" else "(not set)"});
    if (cfg.server_url.len != 0) {
        logo.print("  server: {s}\n", .{cfg.server_url});
    } else if (session.servers.items.len != 0) {
        logo.print("  server: (unset — would use the connected {s})\n", .{session.servers.items[0].base});
    } else {
        logo.print("  server: (not set)\n", .{});
    }
    logo.print("  token:  {s}\n", .{if (cfg.server_token.len != 0) "(set, hidden)" else "(not set — reuses a matching /connect token)"});
}

/// The resolved stencil-server endpoint + bearer for one call. `url` is owned by the
/// caller; `token` borrows from the live connection or the config.
const ServerAuth = struct { url: []u8, token: []const u8 };

/// Contract §5 (cli row): when /connect-ed to the configured serverUrl (or, when empty,
/// the first connection) reuse the live session token; else fall back to
/// STENCIL_LLM_SERVER_TOKEN. Null (with a message) when there is no server to talk to.
fn resolveServerAuth(session: *Session, cfg: *const llm.Config) !?ServerAuth {
    const gpa = session.gpa;
    if (cfg.server_url.len == 0) {
        if (session.servers.items.len != 0) {
            const c = &session.servers.items[0];
            return .{ .url = try gpa.dupe(u8, c.base), .token = c.token };
        }
        logo.err("the stencil-server provider needs a server — '/connect <url>' first, or '/llm server <url>' / STENCIL_LLM_SERVER_URL\n", .{});
        return null;
    }
    const base = try server.normalizeBase(gpa, cfg.server_url);
    if (session.findServer(base)) |c| return .{ .url = base, .token = c.token };
    return .{ .url = base, .token = cfg.server_token };
}

/// Build the console-context system-prompt suffix over `arena` (freed by the caller): one
/// llm.ConsoleServer per live connection — base URL plus, when `with_projects`, best-effort
/// project names. Tokens have no field to ride in; only c.base ever leaves here.
fn consoleContext(session: *Session, arena: std.mem.Allocator, with_projects: bool) error{OutOfMemory}![]u8 {
    const servers = try arena.alloc(llm.ConsoleServer, session.servers.items.len);
    for (session.servers.items, 0..) |*c, i| {
        servers[i] = .{
            .url = c.base,
            .active = session.remote_url != null and std.mem.eql(u8, session.remote_url.?, c.base),
            .projects = if (with_projects) try projectNames(c, arena) else null,
        };
    }
    const active: []const u8 = if (session.hasRemote()) (session.label orelse "") else "";
    return llm.consoleContextAlloc(arena, servers, active);
}

/// One server's project names (arena-owned), or null when the listing fails — the
/// context then omits that server's line rather than claiming it holds nothing.
fn projectNames(c: *server.Client, arena: std.mem.Allocator) error{OutOfMemory}!?[]const []const u8 {
    const infos = c.listProjectInfos() catch return null;
    defer server.freeProjectList(c.gpa, infos);
    const names = try arena.alloc([]const u8, infos.len);
    for (infos, 0..) |p, i| names[i] = try arena.dupe(u8, p.name);
    return names;
}

/// `/prompt <text>` (alias `/p`) — send the prompt, the working image, and its §7 edge map
/// (each within the 8 MiB cap) to the provider, print the reply, execute the op-plan, and
/// render variants. This turn's `/upload`s ride along in order for §2.1 `image` ops.
pub fn doPrompt(session: *Session, io: std.Io, arg: []const u8) !void {
    const typed = std.mem.trim(u8, arg, " \t");
    if (typed.len == 0) {
        logo.err("prompt needs text — e.g. '/prompt rotate this left and make it b&w'\n", .{});
        return;
    }
    const gpa = session.gpa;
    // The turn owns this upload set: whatever happens, the next /upload starts a new one.
    defer session.consumeAttachments();
    // §10 clearChat is deferred to the END of the turn: a defer runs on every exit, after
    // the continuation round has settled.
    defer finishChatClear(session);
    // Contract §11.4: "2" or "1,3" answers the last ask card by number, expanded to the
    // option LABELS every other surface sends. Anything else is an ordinary prompt and
    // simply drops the card — the conversation is never blocked.
    var answer_buf: ?[]u8 = null;
    defer if (answer_buf) |b| gpa.free(b);
    const text = blk: {
        if (session.ask_options.len == 0) break :blk typed;
        const resolved = try llm.resolveAskAnswer(gpa, session.ask_options, session.ask_multi, typed);
        session.clearAsk();
        if (resolved) |r| {
            answer_buf = r;
            logo.print("→ {s}\n", .{r});
            break :blk r;
        }
        break :blk typed;
    };
    const cfg = try session.llmConfig();

    // stencil-server: resolve the endpoint + bearer, preferring a live /connect token.
    var server_url: []u8 = &.{};
    defer if (server_url.len != 0) gpa.free(server_url);
    var server_token: []const u8 = "";
    if (cfg.provider == .stencil_server) {
        const auth = (try resolveServerAuth(session, cfg)) orelse return; // message printed
        server_url = auth.url;
        server_token = auth.token;
    }

    // §7 auto-continuation: a plan that LOADED a picture but drew no layout could not have
    // traced it (the attachment rode along before it existed) — re-send the turn ONCE with
    // the new image in hand.
    var turn_text: []const u8 = text;
    // The continuation round RESTATES the request: with /chat off there is no history at
    // all, so a bare "continue" note would leave the model with no idea what was asked.
    var cont_text: ?[]u8 = null;
    defer if (cont_text) |c| gpa.free(c);
    // The console-context suffix (the §4 dynamic suffix): connections (URLs only — never
    // tokens), the active project, and each server's project names (best-effort fetch).
    var ctx_arena = std.heap.ArenaAllocator.init(gpa);
    defer ctx_arena.deinit();
    const console_ctx: []const u8 = consoleContext(session, ctx_arena.allocator(), true) catch "";
    var round: usize = 0;
    while (round < 2) : (round += 1) {
        // Attach the working image for vision (all three providers take images per the
        // contract); over the cap it is skipped with a note, never downscaled.
        const b64: ?[]const u8 = if (session.hasImage()) try promptImageB64(session) else null;
        // §7 edge map: the working snapshot run through the core contour filter rides as a
        // SECOND image, current turn only. Dropped with the snapshot, or alone when only it
        // is over the cap — and the suffix sentence rides exactly when it does.
        var edge_b64: ?[]u8 = null;
        defer if (edge_b64) |e| gpa.free(e);
        if (b64 != null) edge_b64 = try edgeMapB64(session, llm.max_image_bytes);

        var imgs: std.ArrayList([]const u8) = .empty;
        defer imgs.deinit(gpa);
        if (b64) |w| try imgs.append(gpa, w);
        if (edge_b64) |e| try imgs.append(gpa, e);
        // §2.1: this turn's uploads ride after the working snapshot in upload order — what
        // an `image` op indexes. One that cannot attach drops the whole set rather than
        // shifting the numbering the model plans against.
        var att_b64: std.ArrayList([]u8) = .empty;
        defer {
            for (att_b64.items) |x| gpa.free(x);
            att_b64.deinit(gpa);
        }
        if (session.attachments.items.len > 1) {
            for (session.attachments.items) |at| {
                const enc = try attachmentB64(gpa, at, llm.max_image_bytes);
                if (enc) |e| {
                    try att_b64.append(gpa, e);
                } else {
                    logo.note("'{s}' could not be attached — sending this turn's uploads as one image\n", .{at.label});
                    for (att_b64.items) |x| gpa.free(x);
                    att_b64.clearRetainingCapacity();
                    break;
                }
            }
            for (att_b64.items) |e| try imgs.append(gpa, e);
        }
        // The suffix: console context always, plus the §7 edge-map sentence when it rides.
        const suffix: []u8 = if (edge_b64 != null and console_ctx.len != 0)
            try std.mem.join(gpa, "\n\n", &.{ console_ctx, llm.edge_map_suffix })
        else if (edge_b64 != null)
            try gpa.dupe(u8, llm.edge_map_suffix)
        else
            try gpa.dupe(u8, console_ctx);
        defer gpa.free(suffix);

        // What is happening, in the user's terms — not the wire's. The endpoint is operator
        // detail (it is one `/llm` away); what matters while the screen sits still is that the
        // assistant is thinking, roughly how long that can take, and that Ctrl-C ends it.
        if (session.cancel_poll != null) {
            logo.print("⏳ thinking… this can take a minute (Ctrl-C to cancel)\n", .{});
        } else {
            logo.print("⏳ thinking… this can take a minute\n", .{});
        }

        // With /chat on the saved conversation is replayed (text-only, §7/§12) before the
        // current turn; off, the request is byte-for-byte the plain single-turn one.
        // The system prompt is §4 + the console settings-op block (llm.consoleSystemPrompt()).
        const history: []const llm.Turn = if (session.chat_on) session.chat_history.items else &.{};
        var req = try llm.buildRequestWithSystem(gpa, cfg, llm.consoleSystemPrompt(), turn_text, imgs.items, server_url, server_token, history, suffix);
        defer req.deinit(gpa);
        const body = llm.postJson(gpa, io, req.url, req.auth, req.body, promptWaiter(session)) catch return; // message printed
        defer gpa.free(body);

        const extracted = try llm.extractReply(gpa, cfg.provider, body);
        defer extracted.deinit(gpa);
        switch (extracted) {
            // stencil-server stopReason contract: truncated/refused replies are chat errors,
            // NEVER parsed as plans.
            .truncated => logo.err("the LLM response was truncated (stopReason \"max_tokens\") and was not parsed as a plan\n", .{}),
            .refusal => |t| if (t.len != 0) {
                logo.err("the LLM refused to answer: {s}\n", .{t});
            } else {
                logo.err("the LLM refused to answer (stopReason \"refusal\")\n", .{});
            },
            .bad_reply => |d| logo.err("unexpected LLM response: {s}\n", .{d}),
            .text => |raw| {
                if (!try runPlan(session, io, raw, turn_text) or round != 0) return;
                // The note is llm.continuation_note so the §12.1 gate that refuses it in the
                // persisted document and this writer can never drift apart.
                cont_text = std.fmt.allocPrint(gpa, "{s}\n\n" ++ llm.continuation_note, .{text}) catch return;
                turn_text = cont_text.?;
                continue;
            },
        }
        return; // every non-text outcome above is terminal
    }
}

/// The §10 clearChat deferral, run once the WHOLE turn (continuation included) settled:
/// confirm in-app, then take the exact /chat clear path (history + best-effort server
/// chat-file delete). Declining is a "clear canceled" note, never a failed plan.
fn finishChatClear(session: *Session) void {
    if (!session.pending_chat_clear) return;
    session.pending_chat_clear = false;
    const ask = session.confirm_fn orelse {
        logo.print("clear canceled\n", .{});
        return;
    };
    if (!ask(session.confirm_ctx, "Clear this conversation's chat history?")) {
        logo.print("clear canceled\n", .{});
        return;
    }
    handlers.doChat(session, "clear");
}

/// The working image as a base64 PNG, borrowed from a session cache keyed by a pixel
/// digest so a no-edit follow-up re-sends without re-encoding. Null (with a note) when
/// over the cap or failing to encode — those stay uncached so the note reprints per turn.
fn promptImageB64(session: *Session) error{OutOfMemory}!?[]const u8 {
    const gpa = session.gpa;
    const cur = session.current().*;
    var hasher = std.hash.Wyhash.init(0);
    hasher.update(std.mem.asBytes(&cur.width)); // pixels alone can't tell 2×3 from 3×2
    hasher.update(cur.pixels);
    const digest = hasher.final();
    if (session.prompt_b64) |cached| {
        if (digest == session.prompt_digest) return cached;
    }
    const png = image.encode(gpa, cur, .png) catch |e| {
        logo.note("could not encode the image for attachment ({s}) — sending text only\n", .{@errorName(e)});
        return null;
    };
    defer gpa.free(png);
    if (png.len > llm.max_image_bytes) {
        logo.note("the working image encodes to {d} bytes — over the {d} MiB attachment cap, sending text only\n", .{ png.len, llm.max_image_bytes / (1024 * 1024) });
        return null;
    }
    const out = try gpa.alloc(u8, std.base64.standard.Encoder.calcSize(png.len));
    _ = std.base64.standard.Encoder.encode(out, png);
    if (session.prompt_b64) |old| gpa.free(old);
    session.prompt_b64 = out;
    session.prompt_digest = digest;
    return out;
}

/// One §2.1 turn attachment as a base64 PNG for the wire (the wire mapping sends
/// `image/png` only, so a JPEG/WebP upload is re-encoded). Null — never fatal — when it
/// cannot be decoded/encoded or lands over `cap`. Caller owns the result.
fn attachmentB64(gpa: std.mem.Allocator, at: Attachment, cap: usize) error{OutOfMemory}!?[]u8 {
    var img = image.decode(gpa, at.bytes) catch return null;
    defer img.deinit(gpa);
    const png = image.encode(gpa, img, .png) catch return null;
    defer gpa.free(png);
    if (png.len > cap) return null;
    const out = try gpa.alloc(u8, std.base64.standard.Encoder.calcSize(png.len));
    _ = std.base64.standard.Encoder.encode(out, png);
    return out;
}

/// The §7 edge map: the working image through the core contour filter (the `filter` op's
/// "contour" Sobel pass), as base64 PNG. Null (dropping just the edge map) when over `cap`
/// or failing to encode. Current-turn only, never cached. Caller owns the result.
fn edgeMapB64(session: *Session, cap: usize) error{OutOfMemory}!?[]u8 {
    return contourB64(session.gpa, session.current().*, cap);
}

/// Contour + PNG-encode + base64 a COPY of `src`; null over `cap` or on encode failure.
fn contourB64(gpa: std.mem.Allocator, src: image.Rgba8, cap: usize) error{OutOfMemory}!?[]u8 {
    var img = image.Rgba8{ .width = src.width, .height = src.height, .pixels = try gpa.dupe(u8, src.pixels) };
    defer img.deinit(gpa);
    core.applyContour(img.pixels, @intCast(img.width), @intCast(img.height));
    const png = image.encode(gpa, img, .png) catch return null;
    defer gpa.free(png);
    if (png.len > cap) return null;
    const out = try gpa.alloc(u8, std.base64.standard.Encoder.calcSize(png.len));
    _ = std.base64.standard.Encoder.encode(out, png);
    return out;
}

/// Parse the raw reply and execute it (§1: an invalid plan executes nothing; §3.0: the
/// turn ends at the plan): print the reply, run the top-level actions, render variants;
/// /chat remembers `user_text` + the DISPLAYED reply (§12). True = §7 wants one re-send.
fn runPlan(session: *Session, io: std.Io, raw: []const u8, user_text: []const u8) !bool {
    const gpa = session.gpa;
    switch (try llm.parsePlan(gpa, raw)) {
        .invalid => |msg| {
            defer gpa.free(msg);
            logo.err("{s}\n", .{msg});
        },
        .plan => |p| {
            var plan = p;
            defer plan.deinit();
            logo.print("{s}\n", .{plan.reply});
            if (session.chat_on) {
                session.appendChatTurn(.user, user_text) catch {};
                session.appendChatTurn(.assistant, plan.reply) catch {};
            }
            for (plan.warnings) |w| logo.note("{s}\n", .{w});
            // §11: a plan may also ASK. Printed after the reply and remembered, so the next
            // /prompt can answer it by number. A chat-only turn can carry one too.
            if (plan.ask) |ask| try showAsk(session, ask);
            if (plan.chat_only) return false;
            // The console's working input is never a video, so a frame op anywhere is a
            // plan-level error: nothing executes (contract §2).
            if (plan.hasFrameOp()) {
                logo.err("the frame op needs a video input — the console session works on a still image\n", .{});
                return false;
            }
            // §10 openUrl guard: the model may only ECHO the user — a URL absent from the
            // user's own messages this conversation fails the whole plan, nothing executes.
            const user_turns: []const llm.Turn = if (session.chat_on) session.chat_history.items else &.{};
            for (plan.actions) |a| switch (a) {
                .open_url => |o| if (!llm.urlEchoedByUser(user_turns, user_text, o.url)) {
                    logo.err("openUrl blocked: \"{s}\" is not a URL you gave in this conversation\n", .{o.url});
                    return false;
                },
                // The same rule for local files: the assistant may read and write only where
                // the user themselves pointed it, so a plan can never go looking through the
                // disk or drop results somewhere the user never named.
                .open_file => |f| if (!llm.pathEchoedByUser(user_turns, user_text, f.path)) {
                    logo.err("openFile blocked: \"{s}\" is not a path you gave in this conversation\n", .{f.path});
                    return false;
                },
                .save => |s| if (s.path.len != 0 and !llm.pathEchoedByUser(user_turns, user_text, s.path)) {
                    logo.err("save blocked: \"{s}\" is not a path you gave in this conversation\n", .{s.path});
                    return false;
                },
                else => {},
            };
            var edited = false;
            // Plan coordinates are written in the frame of the §7 snapshot (llm-contract §1),
            // so the crop/rotate actions that execute are accumulated as frame steps and a
            // later layout's points are re-mapped through them (and clamped) before drawing.
            var frame_steps: std.ArrayList(layout_mod.FrameStep) = .empty;
            defer frame_steps.deinit(gpa);
            // §2.1: which `/upload`ed attachment the last `image` op adopted (1-based) —
            // an unnamed `save` names its project after it.
            var active: ?usize = null;
            for (plan.actions) |a| {
                if (!applyPlanAction(session, io, a, &edited, &frame_steps, &active)) break; // message printed; stop here
            }
            if (edited) remoteEvents.markDirty(session); // debounced, flushed at the prompt boundary
            // Variants branch from the post-actions state, so their snapshot-frame
            // coordinates continue through the top-level steps accumulated above.
            renderVariants(session, io, &plan, frame_steps.items);
            return plan.loadsWithoutTracing() and session.hasImage();
        },
    }
    return false;
}

/// Print an `ask` card as a numbered list (contract §11.4: no previews in a console — the
/// options keep their labels) and remember the labels so the next /prompt can answer by
/// number. A card replaces any earlier one: only the latest question is answerable.
fn showAsk(session: *Session, ask: llm.Ask) !void {
    const gpa = session.gpa;
    session.clearAsk();
    logo.print("\n{s}\n", .{ask.question});
    for (ask.options, 0..) |o, i| logo.print("  {d}. {s}\n", .{ i + 1, o.label });
    if (ask.allow_custom) logo.print("  or type your own: {s}\n", .{ask.custom_label});
    // Two calls, not a runtime-selected format: logo.print's format is comptime.
    if (ask.multi)
        logo.print("answer with /p <numbers> (e.g. '/p 1,3'), or just say what you want\n", .{})
    else
        logo.print("answer with /p <number> (e.g. '/p 2'), or just say what you want\n", .{});

    var owned = try gpa.alloc([]u8, ask.options.len);
    var filled: usize = 0;
    errdefer {
        for (owned[0..filled]) |o| gpa.free(o);
        gpa.free(owned);
    }
    for (ask.options, 0..) |o, i| {
        owned[i] = try gpa.dupe(u8, o.label);
        filled = i + 1;
    }
    session.ask_options = owned;
    session.ask_multi = ask.multi;
}

/// Apply one top-level plan action through the SAME session operations the console
/// commands use. False = the action failed (execution stops); `edited` set on an undoable/
/// layout change. Crop/rotate append to `frame_steps`; a layout re-maps through them (§1).
fn applyPlanAction(
    session: *Session,
    io: std.Io,
    a: llm.Action,
    edited: *bool,
    frame_steps: *std.ArrayList(layout_mod.FrameStep),
    active: *?usize,
) bool {
    const gpa = session.gpa;
    switch (a) {
        // The image-transforming ops need a working image, like their console commands.
        .crop, .rotate, .filter, .layout => if (!session.hasImage()) {
            ui.noImage();
            return false;
        },
        else => {},
    }
    switch (a) {
        .crop => |c| {
            const cur = session.current();
            const rect = resolvePlanCrop(gpa, cur.width, cur.height, c) orelse return false; // message printed
            session.applyCrop(rect) catch return false;
            frame_steps.append(gpa, .{ .crop = .{ .x = @floatFromInt(rect.x), .y = @floatFromInt(rect.y) } }) catch return false;
            edited.* = true;
            ui.ack(session, "cropped");
        },
        .rotate => |r| {
            // Record the PRE-rotate view dims the point mapping turns within.
            const cur = session.current().*;
            session.applyRotate(rotateQuarters(r.dir, r.times)) catch return false;
            frame_steps.append(gpa, .{ .rotate = .{
                .quarters = rotateQuarters(r.dir, r.times),
                .w = @floatFromInt(cur.width),
                .h = @floatFromInt(cur.height),
            } }) catch return false;
            edited.* = true;
            ui.ack(session, "rotated");
        },
        .filter => |f| {
            const mode = filterArg(f);
            if (!handlers.applyFilterArg(session, mode)) {
                logo.err("unknown filter \"{s}\"\n", .{mode});
                return false;
            }
            edited.* = true;
            ui.ack(session, mode);
        },
        .layout => |l| {
            // §4: an empty "lines" array REMOVES every drawn line — the /apply path's
            // replace mode over nothing, undoable like the draw it undoes.
            if (std.mem.eql(u8, l.lines_json, "[]")) {
                session.setLines("{\"lines\":[]}") catch return false;
                edited.* = true;
                ui.ack(session, "lines removed");
                return true;
            }
            // Re-map the snapshot-frame points through the plan's earlier crop/rotate and
            // clamp into the current view (identity steps still clamp).
            const cur = session.current().*;
            const remapped = layout_mod.remapLinesArrayAlloc(gpa, l.lines_json, frame_steps.items, cur.width, cur.height) catch return false;
            defer gpa.free(remapped);
            const doc = linesDoc(gpa, remapped) catch return false;
            defer gpa.free(doc);
            session.addLines(doc) catch return false;
            edited.* = true;
            ui.ack(session, "drawn");
        },
        .formula => |f| {
            // §2 enabled-form: the formulas on/off toggle (/formula on|off) — false
            // restores identity, keeping the expressions like the command does.
            if (f.enabled) |on| {
                session.setAllowFormulas(on);
                if (on) handlers.printFormula(session) else logo.print("formulas off (expressions kept)\n", .{});
                edited.* = true; // rides the saved/synced layout, like /formula
                return true;
            }
            const ok = session.setFormula(f.axis, f.expr) catch return false;
            if (!ok) {
                logo.err("invalid {c} formula: {s}\n", .{ f.axis, f.expr });
                return false;
            }
            edited.* = true; // rides the saved/synced layout, like /formula
            handlers.printFormula(session);
        },
        .page => |pf| {
            // §2 custom form: explicit cm dims — the /format custom path.
            if (pf.format.len == 0) {
                session.setPageSize("custom") catch return false;
                session.custom_page_w = pf.width;
                session.custom_page_h = pf.height;
                edited.* = true; // rides the saved/synced layout, like /format
                logo.print("page format set to custom ({d}×{d}cm)\n", .{ pf.width, pf.height });
                return true;
            }
            const name = core.canonicalPageFormat(pf.format) orelse {
                logo.err("unknown page format '{s}'\n", .{pf.format});
                return false;
            };
            session.setPageSize(name) catch return false;
            edited.* = true; // rides the saved/synced layout, like /format
            logo.print("page format set to {s}\n", .{name});
        },
        .blank => |b| {
            const blank = acquirePlanBlank(session, b, null) orelse return false; // message printed
            session.loadImage(blank.img, "blank", true, .png, null) catch return false;
            if (blank.custom_w > 0) {
                // §2 explicit cm dims: the page pick becomes custom, like /blank <w> <h>.
                session.setPageSize("custom") catch {};
                session.custom_page_w = blank.custom_w;
                session.custom_page_h = blank.custom_h;
            } else if (blank.page) |p| session.setPageSize(p) catch {};
            // A fresh picture starts a fresh frame — earlier crop/rotate steps don't apply.
            frame_steps.clearRetainingCapacity();
            edited.* = true;
            ui.redraw(session);
        },
        // §2.1: adopt the turn's Nth `/upload`ed image as the working image. An index
        // this turn cannot satisfy costs the ACTION only — the plan carries on.
        .image => |im| {
            const list = session.attachments.items;
            if (im.index == 0 or im.index > list.len) {
                logo.note("skipped switching to attached image {d} — this turn uploaded {d} image(s)\n", .{ im.index, list.len });
                return true;
            }
            const at = list[im.index - 1];
            var img = image.decode(gpa, at.bytes) catch {
                logo.note("skipped attached image {d} — '{s}' could not be decoded\n", .{ im.index, at.label });
                return true;
            };
            const bytes = gpa.dupe(u8, at.bytes) catch {
                img.deinit(gpa);
                return false;
            };
            // loadImage takes the pixels + a private copy of the encoded source (so the
            // attachment stays available for a later `image` op, or a second save).
            const label = at.label;
            session.loadImage(img, label, at.temp, at.fmt, bytes) catch return false;
            frame_steps.clearRetainingCapacity(); // a fresh picture = a fresh coordinate frame
            active.* = im.index;
            edited.* = true;
            ui.redraw(session);
        },
        // §2.1: persist the current image + layout through the same `/save <x>.stencil`
        // path the console command uses. Nothing loaded is a skipped action, not a stop.
        .save => |s| {
            if (!session.hasImage()) {
                logo.note("skipped save — no working image to save\n", .{});
                return true;
            }
            const path = planSavePath(session, io, s.name, active.*, s.path) orelse return true; // message printed
            defer gpa.free(path);
            // A destination naming an image format writes the picture (what /save does with
            // one); everything else is the project bundle.
            if (project.isStencilPath(path)) {
                handlers.saveProject(session, io, path) catch return false;
            } else {
                const page_label = session.pageFormatLabel() catch return false;
                defer gpa.free(page_label);
                pipeline.writeOutputLabeled(gpa, io, session.current().*, path, session.default_fmt, page_label) catch return true;
            }
        },
        // ── §2 undo/redo/reset (top-level only): the console's OWN history, through the
        // same /undo, /redo and /reset paths — steps running out is a note, never a
        // failed plan (contract §2). Like the commands, they queue no sync of their own. ──
        .undo => |u| planStep(session, false, u.steps),
        .redo => |r| planStep(session, true, r.steps),
        .reset => handlers.doReset(session), // "no image loaded" is its own note
        // ── Console-settings ops (the §10-analog profile): they run the SAME handlers
        // the /theme, /connect, /disconnect, /reconnect, /delete and /drop commands use,
        // and every miss is a printed note, never a failed plan. None of them edits the
        // image. ──
        // A preset rides the same /theme path as a hex — its name table resolves it,
        // and an unknown name is /theme's own note + skip (§10).
        .accent => |c| handlers.doTheme(session, if (c.preset.len != 0) c.preset else c.color),
        .connect => |c| planConnect(session, io, c.server),
        .disconnect => |c| planDisconnect(session, c.server),
        .reconnect => |c| planReconnect(session, io, c.server),
        .delete => |d| handlers.doDelete(io, d.path) catch {}, // same guards + messages as /delete
        // §10 clear: drop the working image + its lines — the console's /drop (an empty
        // console is its own note when nothing is loaded).
        .clear => handlers.doDrop(session),
        // §10 clearChat: only ARM the deferred clear — the confirm and the actual
        // /chat-clear run once the WHOLE /prompt turn settles (finishChatClear).
        .clear_chat => session.pending_chat_clear = true,
        // §10 openFile (user-echo pre-checked by runPlan): the same load /upload performs —
        // .stencil restores a project, .json draws its layout, anything else is a picture
        // (or a video's first frame).
        .open_file => |f| {
            const path = pipeline.expandHome(gpa, f.path) catch return false;
            defer gpa.free(path);
            if (project.isStencilPath(path)) {
                handlers.openProject(session, io, path) catch return false;
                frame_steps.clearRetainingCapacity();
                active.* = null;
                return true;
            }
            if (std.ascii.endsWithIgnoreCase(path, ".json")) {
                const bytes = pipeline.loadLayoutBytes(gpa, io, path) catch return true; // message printed
                defer gpa.free(bytes);
                session.addLines(bytes) catch return false;
                ui.redraw(session);
                return true;
            }
            const src = pipeline.acquireInput(gpa, io, path, 0) catch return true; // message printed
            session.loadImage(src.img, path, false, src.default_fmt, src.bytes) catch return false;
            frame_steps.clearRetainingCapacity(); // a fresh picture = a fresh coordinate frame
            active.* = null;
            ui.redraw(session);
        },
        // §10 openUrl (user-echo pre-checked by runPlan): the same load /upload <url>
        // performs, synchronous — later actions see the fetched picture, and an unnamed
        // save derives its name from the URL label, like an upload's.
        .open_url => |o| {
            if (o.incognito) logo.note("incognito is not a console concept — loading normally\n", .{});
            const src = pipeline.acquireInput(gpa, io, o.url, 0) catch return false; // message printed
            session.loadImage(src.img, o.url, true, src.default_fmt, src.bytes) catch return false;
            frame_steps.clearRetainingCapacity(); // a fresh picture = a fresh coordinate frame
            active.* = null; // the loaded URL, not an earlier attachment, names a save now
            edited.* = true;
            ui.redraw(session);
        },
        // §10 copy: the console's /copy. No working image is a note + skip, never a stop.
        .copy => {
            if (!session.hasImage()) {
                logo.note("skipped copy — no working image to copy\n", .{});
                return true;
            }
            attachments.doCopy(session, io) catch return false;
        },
        .frame => return false, // pre-checked by runPlan (plan-level error)
    }
    return true;
}

/// A plan `connect`: resolve ONLY against servers this session already connected to (§10 —
/// the model can never introduce a host). A live match is a note, a known disconnected one
/// reconnects through doConnect, anything else points at '/connect'.
fn planConnect(session: *Session, io: std.Io, want: []const u8) void {
    switch (llm.resolveServer(session.known_servers.items, want)) {
        .index => |i| {
            const url = session.known_servers.items[i];
            if (session.findServer(url) != null) {
                logo.print("already connected to {s}\n", .{url});
                return;
            }
            handlers.doConnect(session, io, url) catch {};
        },
        .ambiguous => logo.note("skipped connect — \"{s}\" matches several of this session's servers; use the full URL\n", .{want}),
        .none => logo.note("skipped connect — \"{s}\" is not a server you connected this session; run '/connect <url>' yourself\n", .{want}),
    }
}

/// A plan `disconnect`: resolved against the LIVE connections (like §10 — exact URL,
/// else unique host), then through the same path the /disconnect command takes.
fn planDisconnect(session: *Session, want: []const u8) void {
    const gpa = session.gpa;
    var urls: std.ArrayList([]const u8) = .empty;
    defer urls.deinit(gpa);
    for (session.servers.items) |*c| urls.append(gpa, c.base) catch return;
    switch (llm.resolveServer(urls.items, want)) {
        .index => |i| {
            // Resolve to the base COPY first — doDisconnect frees the client it drops.
            const base = gpa.dupe(u8, urls.items[i]) catch return;
            defer gpa.free(base);
            handlers.doDisconnect(session, base) catch {};
        },
        .ambiguous => logo.note("skipped disconnect — \"{s}\" matches several connected servers; use the full URL\n", .{want}),
        .none => logo.note("skipped disconnect — not connected to \"{s}\" ('/connections' lists them)\n", .{want}),
    }
}

/// A plan `reconnect`: connect's resolution (§10 — exact URL, else unique host, over the
/// servers the user /connect-ed THIS session), then the same path the /reconnect command
/// takes — which itself notes a match that is not currently live. Misses are notes.
fn planReconnect(session: *Session, io: std.Io, want: []const u8) void {
    switch (llm.resolveServer(session.known_servers.items, want)) {
        .index => |i| handlers.doReconnect(session, io, session.known_servers.items[i]) catch {},
        .ambiguous => logo.note("skipped reconnect — \"{s}\" matches several of this session's servers; use the full URL\n", .{want}),
        .none => logo.note("skipped reconnect — \"{s}\" is not a server you connected this session; run '/connect <url>' yourself\n", .{want}),
    }
}

/// A plan §2 undo/redo: step the session's edit history up to `steps` HISTORY entries
/// through the same acknowledgement path the /undo and /redo commands use. Steps running
/// out mid-way is a note (contract §2), never a failed plan.
fn planStep(session: *Session, comptime redo: bool, steps: u8) void {
    var moved: u8 = 0;
    if (session.hasImage()) {
        while (moved < steps) : (moved += 1) {
            const ok = if (redo) session.redo() else session.undo();
            if (!ok) break;
        }
    }
    handlers.doStep(
        session,
        moved != 0,
        if (redo) "redone" else "undone",
        if (redo) "nothing to redo (at the latest edit)" else "nothing to undo (at the original)",
    );
    if (moved != 0 and moved < steps)
        logo.note("only {d} of {d} {s} step(s) were available\n", .{ moved, steps, if (redo) "redo" else "undo" });
}

/// Where a §2.1 `save` writes: `<name>.stencil` in the cwd — name from the action, else
/// the ACTIVE attachment, else the working image's label — suffixed " 2"/" 3"… on
/// collision. Caller owns the path; null (with a note) when nothing survives sanitizing.
fn planSavePath(session: *Session, io: std.Io, name: []const u8, active: ?usize, dest: []const u8) ?[]u8 {
    const gpa = session.gpa;
    // A destination the user named (runPlan already checked they wrote it): a file path is
    // taken as given, a folder gets "<name>.png" — the format /save writes by default.
    if (dest.len != 0) {
        const home = pipeline.expandHome(gpa, dest) catch return null;
        if (looksLikeFilePath(home)) return home;
        defer gpa.free(home);
        const stem = saveStem(session, name, active) orelse return null;
        const sep: []const u8 = if (home.len != 0 and home[home.len - 1] == '/') "" else "/";
        return std.fmt.allocPrint(gpa, "{s}{s}{s}.png", .{ home, sep, stem }) catch null;
    }
    const stem = saveStem(session, name, active) orelse return null;
    const dir = std.Io.Dir.cwd();
    var path = std.fmt.allocPrint(gpa, "{s}.stencil", .{stem}) catch return null;
    var n: usize = 2;
    while (n < 100) : (n += 1) {
        dir.access(io, path, .{}) catch break; // free name (or unreadable) → take it
        gpa.free(path);
        path = std.fmt.allocPrint(gpa, "{s} {d}.stencil", .{ stem, n }) catch return null;
    }
    return path;
}

/// The project stem an unnamed (or oddly named) plan save falls back to: the op's own name,
/// else the adopted attachment's label, else the session label. Directory and extension are
/// dropped, so a model-supplied "../x.png" can never steer the write.
fn saveStem(session: *Session, name: []const u8, active: ?usize) ?[]const u8 {
    const from_attachment = if (active) |i| session.attachments.items[i - 1].label else "";
    const raw = if (name.len != 0) name else if (from_attachment.len != 0) from_attachment else (session.label orelse "project");
    const stem = commands.projectBaseName(std.mem.trim(u8, raw, " \t"));
    if (stem.len == 0 or std.mem.trim(u8, stem, ".").len == 0) {
        logo.note("skipped save — \"{s}\" is not a usable project name\n", .{raw});
        return null;
    }
    return stem;
}

/// Whether a save destination names a FILE (an extension this app writes) rather than a folder.
fn looksLikeFilePath(path: []const u8) bool {
    if (path.len == 0 or path[path.len - 1] == '/') return false;
    return llm.understoodPath(path);
}

/// Map the contract's rotate op onto the session's clockwise quarter-turn count
/// (`right` = clockwise, matching `/rotate n`).
fn rotateQuarters(dir: llm.Dir, times: u8) i32 {
    const n: i32 = times;
    return if (dir == .right) n else -n;
}

/// The `/filter`-style argument for a plan filter op (`custom` passes the tint colour as
/// the filter value, per the contract's CLI mapping).
fn filterArg(f: anytype) []const u8 {
    return if (f.mode == .custom) f.tint else @tagName(f.mode);
}

/// The page a plan blank op lands on: its explicit format, else the session's picked page
/// — the same fallback a bare '/blank <color>' uses. The canonical name is core-owned
/// (static), so it survives a load's format reset.
fn blankPage(session: *const Session, format: ?[]const u8) ?[]const u8 {
    return core.canonicalPageFormat(format orelse session.page_size);
}

/// Resolve a plan crop op against a view size: edges serialized to the `/crop` spec
/// grammar and routed through the same pipeline resolver ("album" rides as the same
/// modifier flag, §10). Null when invalid (resolver prints). Shared with variants.
fn resolvePlanCrop(gpa: std.mem.Allocator, w: usize, h: usize, c: llm.CropEdges) ?core.Rect {
    const spec = llm.cropSpecString(gpa, c) catch return null;
    defer gpa.free(spec);
    return pipeline.resolveCropSpec(gpa, w, h, spec, c.album);
}

const PlanBlank = struct { img: image.Rgba8, page: ?[]const u8, custom_w: f64 = 0, custom_h: f64 = 0 };

/// Validate a plan blank op's colour and acquire its page image + canonical page name (the
/// '/blank <color>' fallback); §2 cm dims override the format and come back as custom_w/h.
/// `variant` names the variant for errors (null = top-level). Null on failure; caller owns `img`.
fn acquirePlanBlank(session: *const Session, b: anytype, variant: ?[]const u8) ?PlanBlank {
    const gpa = session.gpa;
    if (core.parseColor(gpa, b.color) == null) {
        if (variant) |stem|
            logo.err("unknown blank colour '{s}' in variant \"{s}\"\n", .{ b.color, stem })
        else
            logo.err("unknown blank colour '{s}'\n", .{b.color});
        return null;
    }
    if (b.width > 0 and b.height > 0) {
        const s = core.defaultBlankSizePx(b.width, b.height, 96.0);
        const img = pipeline.acquireBlank(gpa, .{ .width = @intCast(s.w), .height = @intCast(s.h), .color = b.color }) catch return null;
        return .{ .img = img, .page = null, .custom_w = b.width, .custom_h = b.height };
    }
    const page = blankPage(session, b.format);
    const img = pipeline.acquireBlank(gpa, .{ .page = page, .color = b.color }) catch return null;
    return .{ .img = img, .page = page };
}

/// Wrap a validated plan lines ARRAY into the `{"lines":[…]}` document shape the session's
/// /apply path (layout.zig) consumes. Caller owns the result.
fn linesDoc(gpa: std.mem.Allocator, lines_json: []const u8) error{OutOfMemory}![]u8 {
    return std.fmt.allocPrint(gpa, "{{\"lines\":{s}}}", .{lines_json});
}

/// Render each plan variant as `variant-<label>.png` in the cwd. A variant branches from
/// the state AFTER the top-level actions (§1): a copy of the current view run through the
/// same pipeline blocks; the session is untouched. A failed variant skips; others render.
fn renderVariants(session: *Session, io: std.Io, plan: *const llm.Plan, base_steps: []const layout_mod.FrameStep) void {
    if (plan.variants.len == 0) return;
    const gpa = session.gpa;
    var used: std.ArrayList([]u8) = .empty;
    defer {
        for (used.items) |s| gpa.free(s);
        used.deinit(gpa);
    }
    for (plan.variants, 0..) |v, i| {
        const stem = variantStem(gpa, &used, v.label, i) catch continue;
        renderVariant(session, io, v, stem, base_steps);
    }
}

/// A unique `[a-z0-9-]` file stem for a variant: the sanitized label, falling back to the
/// variant's 1-based position, with a numeric suffix on a collision ("Rotated!" vs
/// "rotated"). Appended to `used`, which owns it.
fn variantStem(gpa: std.mem.Allocator, used: *std.ArrayList([]u8), label: []const u8, i: usize) ![]const u8 {
    var stem = try llm.sanitizeLabel(gpa, label);
    errdefer gpa.free(stem); // frees whatever stem currently holds on any later failure
    if (stem.len == 0) stem = try std.fmt.allocPrint(gpa, "{d}", .{i + 1});
    if (stemTaken(used.items, stem)) {
        var n: usize = 2;
        while (true) : (n += 1) {
            const cand = try std.fmt.allocPrint(gpa, "{s}-{d}", .{ stem, n });
            if (!stemTaken(used.items, cand)) {
                gpa.free(stem);
                stem = cand;
                break;
            }
            gpa.free(cand);
        }
    }
    try used.append(gpa, stem);
    return stem;
}

fn stemTaken(used: []const []u8, stem: []const u8) bool {
    for (used) |s| {
        if (std.mem.eql(u8, s, stem)) return true;
    }
    return false;
}

/// The §6 transport waiter for a console turn: the session's Ctrl-C watch plus its clock, so
/// a slow call can be cancelled by the user or abandoned at the deadline. A session with no
/// watch installed (one-shot CLI, tests) yields the plain blocking call.
fn promptWaiter(session: *Session) llm.Waiter {
    if (session.cancel_poll == null or session.cancel_ctx == null) return .{};
    return .{ .ctx = session.cancel_ctx, .poll = session.cancel_poll };
}

/// Lines waiting to go on a variant at the end of its ops, with the index into `steps` from
/// which they still have to be re-mapped: 0 for a variant's own snapshot-frame layout op,
/// past the base steps for the session's already-drawn lines.
const PendingLines = struct { json: []const u8, steps_start: usize };

fn renderVariant(session: *Session, io: std.Io, v: llm.Variant, stem: []const u8, base_steps: []const layout_mod.FrameStep) void {
    const gpa = session.gpa;
    // Start from the current view WITHOUT its drawn lines: a variant filter recolours the
    // picture, never the annotations, so every line goes on at the end (the session's own
    // layering). A sessionless variant can still begin with a blank op.
    var img: ?image.Rgba8 = null;
    defer if (img) |*m| m.deinit(gpa);
    var pending: std.ArrayList(PendingLines) = .empty;
    defer pending.deinit(gpa);
    // The variant's coordinates are snapshot-frame too (llm-contract §1): its layout
    // points continue through the top-level steps plus the variant's own crop/rotates.
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(gpa);
    steps.appendSlice(gpa, base_steps) catch return;
    if (session.hasImage()) {
        img = session.viewWithoutLines() catch return;
        // The session's lines are already in the current frame — only this variant's own
        // crop/rotates (the steps appended below) still apply to them.
        const drawn = session.state().lines_json;
        if (drawn.len != 0) pending.append(gpa, .{ .json = drawn, .steps_start = steps.items.len }) catch return;
    }
    // The page the wrote line reports: the session's pick, overridden by a page/blank op.
    var page_size: []const u8 = session.page_size;
    var page_w = session.custom_page_w;
    var page_h = session.custom_page_h;

    for (v.actions) |a| switch (a) {
        .crop => |c| {
            const cur = variantImage(&img, stem) orelse return;
            const rect = resolvePlanCrop(gpa, cur.width, cur.height, c) orelse return; // message printed
            pipeline.cropToRect(gpa, cur, rect) catch return;
            steps.append(gpa, .{ .crop = .{ .x = @floatFromInt(rect.x), .y = @floatFromInt(rect.y) } }) catch return;
        },
        .rotate => |r| {
            const cur = variantImage(&img, stem) orelse return;
            const pre_w = cur.width;
            const pre_h = cur.height;
            pipeline.applyRotateBy(gpa, cur, rotateQuarters(r.dir, r.times)) catch return;
            steps.append(gpa, .{ .rotate = .{
                .quarters = rotateQuarters(r.dir, r.times),
                .w = @floatFromInt(pre_w),
                .h = @floatFromInt(pre_h),
            } }) catch return;
        },
        .filter => |f| {
            const cur = variantImage(&img, stem) orelse return;
            pipeline.applyFilterMode(gpa, cur, filterArg(f));
        },
        .layout => |l| {
            _ = variantImage(&img, stem) orelse return; // message printed
            pending.append(gpa, .{ .json = l.lines_json, .steps_start = 0 }) catch return;
        },
        .formula => logo.note("a formula op has no effect on a rendered variant file — skipped in \"{s}\"\n", .{stem}),
        .page => |pf| {
            if (pf.format.len == 0) {
                page_size = "custom";
                page_w = pf.width;
                page_h = pf.height;
            } else if (core.canonicalPageFormat(pf.format)) |name| {
                page_size = name;
                page_w = 0;
                page_h = 0;
            }
        },
        .blank => |b| {
            const blank = acquirePlanBlank(session, b, stem) orelse return; // message printed
            if (img) |*m| m.deinit(gpa);
            img = blank.img;
            steps.clearRetainingCapacity(); // a fresh picture starts a fresh frame
            pending.clearRetainingCapacity(); // …and drops the lines meant for the old one
            if (blank.custom_w > 0) {
                page_size = "custom";
                page_w = blank.custom_w;
                page_h = blank.custom_h;
            } else if (blank.page) |p| {
                page_size = p;
                page_w = 0;
                page_h = 0;
            }
        },
        .frame => return, // pre-checked by runPlan
        // §2 undo/redo/reset, §2.1 image/save + the console-settings ops are top-level
        // only — a variant carrying one was dropped at parse time, so none reach here.
        .undo, .redo, .reset, .image, .save, .accent, .connect, .disconnect, .reconnect, .delete, .open_url, .open_file, .copy, .clear, .clear_chat => return,
    };

    // Every line last, over the finished picture, each re-mapped through the geometry
    // steps that came after it.
    const rendered = variantImage(&img, stem) orelse return;
    for (pending.items) |p| {
        rasterizeVariantLines(gpa, rendered, p.json, steps.items[@min(p.steps_start, steps.items.len)..]);
    }
    const out = rendered.*;
    // The same page-label derivation the session header / wrote lines share.
    const label = pipeline.pageLabelAlloc(gpa, page_size, page_w, page_h, out.width, out.height) catch return;
    defer gpa.free(label);
    const path = std.fmt.allocPrint(gpa, "variant-{s}.png", .{stem}) catch return;
    defer gpa.free(path);
    pipeline.writeOutputLabeled(gpa, io, out, path, .png, label) catch |e| {
        logo.err("could not write {s} ({s})\n", .{ path, @errorName(e) });
    };
}

/// The variant's working image, or a printed error when no image exists yet.
fn variantImage(img: *?image.Rgba8, stem: []const u8) ?*image.Rgba8 {
    if (img.*) |*m| return m;
    logo.err("variant \"{s}\" needs a working image — nothing loaded\n", .{stem});
    return null;
}

/// Rasterize a JSON lines ARRAY onto a variant image — the variant-side twin of the
/// session's own line rendering (both go through layout.zig + core.rasterizeLine). The
/// snapshot-frame points are re-mapped through `steps` and clamped first (§1).
fn rasterizeVariantLines(gpa: std.mem.Allocator, img: *image.Rgba8, lines_json: []const u8, steps: []const layout_mod.FrameStep) void {
    // No steps to follow ⇒ nothing to re-map: draw the points as given, the way the session's
    // own rebuild does. (Re-mapping through an identity chain would still clamp them inward.)
    const remapped: ?[]u8 = if (steps.len == 0)
        null
    else
        layout_mod.remapLinesArrayAlloc(gpa, lines_json, steps, img.width, img.height) catch return;
    defer if (remapped) |r| gpa.free(r);
    const doc = linesDoc(gpa, remapped orelse lines_json) catch return;
    defer gpa.free(doc);
    var parsed = layout_mod.parse(gpa, doc) catch return;
    defer parsed.deinit();
    for (parsed.lines) |line| core.rasterizeLine(img.pixels, @intCast(img.width), @intCast(img.height), line);
}

const testing = std.testing;

// ── /prompt attachments + plan execution (transport stays out, as in the llm tests) ──

/// Collects what the console TELLS the user (the logo.print sink the full-screen console
/// installs), so a test can assert on the notes an action printed.
pub const Capture = struct {
    gpa: std.mem.Allocator,
    buf: std.ArrayList(u8) = .empty,

    pub fn init(gpa: std.mem.Allocator) Capture {
        return .{ .gpa = gpa };
    }

    pub fn deinit(self: *Capture) void {
        self.buf.deinit(self.gpa);
    }

    pub fn install(self: *Capture) void {
        logo.setSink(trampoline, self);
    }

    fn trampoline(ctx: *anyopaque, chunk: []const u8) void {
        const self: *Capture = @ptrCast(@alignCast(ctx));
        self.buf.appendSlice(self.gpa, chunk) catch {};
    }

    pub fn text(self: *const Capture) []const u8 {
        return self.buf.items;
    }
};

/// applyPlanAction for the tests that drive ONE non-save action: a scratch io and a
/// fresh "no attachment adopted yet" state, so each case reads as the action itself.
fn applyOne(
    session: *Session,
    a: llm.Action,
    edited: *bool,
    steps: *std.ArrayList(layout_mod.FrameStep),
) bool {
    var threaded = std.Io.Threaded.init(session.gpa, .{});
    defer threaded.deinit();
    var active: ?usize = null;
    return applyPlanAction(session, threaded.io(), a, edited, steps, &active);
}

/// A quiet logo sink so the LLM helpers' notes don't leak into the test runner's output.
pub fn swallowPrint(_: *anyopaque, _: []const u8) void {}

/// A session over a fresh 4x4 image: white with a black left column (a real edge, so the
/// contour render differs from the original).
fn testSession(a: std.mem.Allocator) !Session {
    var session = Session{ .gpa = a };
    errdefer session.deinit();
    const px = try a.alloc(u8, 4 * 4 * 4);
    @memset(px, 255);
    for (0..4) |y| @memset(px[y * 16 ..][0..3], 0);
    try session.loadImage(.{ .width = 4, .height = 4, .pixels = px }, "test", true, .png, null);
    return session;
}

test "edge map: rides beside the working snapshot; over its own cap only it drops (§7)" {
    const a = testing.allocator;
    var quiet: u8 = 0;
    logo.setSink(swallowPrint, &quiet);
    defer logo.clearSink();
    var session = try testSession(a);
    defer session.deinit();

    const working = (try promptImageB64(&session)).?; // session-owned (cached)
    const edge = (try edgeMapB64(&session, llm.max_image_bytes)).?;
    defer a.free(edge);
    // A distinct second image: the contour render, not a copy of the snapshot.
    try testing.expect(!std.mem.eql(u8, working, edge));

    // A cap only the edge map exceeds drops just it — the snapshot still rides.
    try testing.expect((try edgeMapB64(&session, 8)) == null);
    try testing.expect((try promptImageB64(&session)) != null);
}

test "runPlan: a layout turn ends at its plan — one model round, nothing after it (§3.0)" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = try testSession(a); // 4x4
    defer session.deinit();

    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const raw = "{\"version\":1,\"reply\":\"traced it\",\"actions\":[{\"op\":\"layout\",\"lines\":[" ++
        "{\"points\":[{\"x\":0,\"y\":0},{\"x\":1,\"y\":1}],\"color\":\"#FF0000\"}," ++
        "{\"points\":[{\"x\":3,\"y\":3},{\"x\":2,\"y\":2}],\"color\":\"#0000FF\"}]}]}";
    // False = no §7 continuation, so doPrompt's round loop stops: the turn cost ONE request.
    try testing.expect(!try runPlan(&session, threaded.io(), raw, "outline the shapes"));

    // The whole transcript is the reply plus the draw acknowledgement. Every request this
    // module makes announces itself or its failure through this same sink, so the exact
    // match IS the round count — nothing ran after the plan.
    try testing.expectEqualStrings("traced it\ndrawn -> 4x4 px · A4 21×29.7cm  [2/2]\n", cap.text());
    inline for (.{ "correction", "self-check", "keeping the lines as planned", "sharpen" }) |phrase| {
        try testing.expect(std.mem.indexOf(u8, cap.text(), phrase) == null);
    }

    // The model's traced lines ARE the result: kept verbatim, in plan order.
    const after = session.state().lines_json;
    try testing.expect(std.mem.indexOf(u8, after, "{\"x\":0,\"y\":0}") != null);
    try testing.expect(std.mem.indexOf(u8, after, "{\"x\":3,\"y\":3}") != null);
    try testing.expect(std.mem.indexOf(u8, after, "#FF0000") != null);
    try testing.expect(std.mem.indexOf(u8, after, "#0000FF") != null);
}

test "runPlan: a stray line off the subject is kept as planned — no follow-up round (§3.0)" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = try testSession(a); // 4x4
    defer session.deinit();

    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    // Two lines whose boxes are disjoint — the shape the withdrawn suspect check chased.
    const raw = "{\"version\":1,\"reply\":\"done\",\"actions\":[{\"op\":\"layout\",\"lines\":[" ++
        "{\"points\":[{\"x\":0,\"y\":0},{\"x\":1,\"y\":1}]}," ++
        "{\"points\":[{\"x\":3,\"y\":3},{\"x\":4,\"y\":4}]}]}]}";
    try testing.expect(!try runPlan(&session, threaded.io(), raw, "outline the cat"));
    try testing.expectEqualStrings("done\ndrawn -> 4x4 px · A4 21×29.7cm  [2/2]\n", cap.text());
}

test "plan layout after crop/rotate is re-mapped into the current frame (§1)" {
    const a = testing.allocator;
    var quiet: u8 = 0;
    logo.setSink(swallowPrint, &quiet);
    defer logo.clearSink();

    // An 8x8 white session image.
    var session = Session{ .gpa = a };
    defer session.deinit();
    const px = try a.alloc(u8, 8 * 8 * 4);
    @memset(px, 255);
    try session.loadImage(.{ .width = 8, .height = 8, .pixels = px }, "test", true, .png, null);

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);

    // Crop x1=4px (y kept full so the single-axis aspect derivation stays out of play)
    // → view 4x8 with origin (4,0).
    try testing.expect(applyOne(&session, .{ .crop = .{ .x1 = "4px", .y1 = "0px" } }, &edited, &steps));
    try testing.expectEqual(@as(usize, 4), session.current().width);
    // Rotate right once → view 8x4.
    try testing.expect(applyOne(&session, .{ .rotate = .{ .dir = .right, .times = 1 } }, &edited, &steps));
    try testing.expectEqual(@as(usize, 2), steps.items.len);

    // A layout in the SNAPSHOT frame: (6,2) → crop → (2,2) → rotate(4x8 CW) → (6,2).
    try testing.expect(applyOne(&session, .{ .layout = .{
        .lines_json = "[{\"points\":[{\"x\":6,\"y\":2},{\"x\":5,\"y\":3}],\"color\":\"#FF0000\"}]",
    } }, &edited, &steps));

    const after = session.state().lines_json;
    // (6,2) → (6,2) here (crop then rotate compose); (5,3) → (1,3) → (8−3,1) = (5,1).
    try testing.expect(std.mem.indexOf(u8, after, "{\"x\":6,\"y\":2}") != null);
    try testing.expect(std.mem.indexOf(u8, after, "{\"x\":5,\"y\":1}") != null);
}

test "plan layout with no earlier crop/rotate is clamped but not re-mapped (§1)" {
    const a = testing.allocator;
    var quiet: u8 = 0;
    logo.setSink(swallowPrint, &quiet);
    defer logo.clearSink();
    var session = try testSession(a); // 4x4
    defer session.deinit();

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);
    try testing.expect(applyOne(&session, .{ .layout = .{
        .lines_json = "[{\"points\":[{\"x\":2,\"y\":1},{\"x\":99,\"y\":-7}]}]",
    } }, &edited, &steps));

    const after = session.state().lines_json;
    try testing.expect(std.mem.indexOf(u8, after, "{\"x\":2,\"y\":1}") != null); // untouched
    try testing.expect(std.mem.indexOf(u8, after, "{\"x\":4,\"y\":0}") != null); // clamped into 4x4
}

// ── §2.1 multi-image ops: `/upload` attachments, image switching, .stencil saves ──

/// A solid `w`x`h` PNG, standing in for an uploaded file's encoded bytes.
pub fn pngOf(a: std.mem.Allocator, w: usize, h: usize) ![]u8 {
    const px = try a.alloc(u8, w * h * 4);
    @memset(px, 200);
    var img = image.Rgba8{ .width = w, .height = h, .pixels = px };
    defer img.deinit(a);
    return image.encode(a, img, .png);
}

/// A session over a 4x4 image with two uploads remembered as this turn's attachments:
/// "cat.png" (6x2) and "photos/dog.jpg" (3x5), in upload order.
fn attachedSession(a: std.mem.Allocator) !Session {
    var session = try testSession(a);
    errdefer session.deinit();
    try session.addAttachment("cat.png", try pngOf(a, 6, 2), .png, false);
    try session.addAttachment("photos/dog.jpg", try pngOf(a, 3, 5), .jpeg, false);
    return session;
}

test "plan image op adopts that upload as the working image and resets the frame (§2.1)" {
    const a = testing.allocator;
    var quiet: u8 = 0;
    logo.setSink(swallowPrint, &quiet);
    defer logo.clearSink();
    var session = try attachedSession(a);
    defer session.deinit();

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);

    // A crop first: its frame step must NOT survive the switch to a fresh picture.
    try testing.expect(applyOne(&session, .{ .crop = .{ .x1 = "1px", .y1 = "0px" } }, &edited, &steps));
    try testing.expectEqual(@as(usize, 1), steps.items.len);

    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    var active: ?usize = null;
    try testing.expect(applyPlanAction(&session, threaded.io(), .{ .image = .{ .index = 2 } }, &edited, &steps, &active));
    try testing.expectEqual(@as(usize, 3), session.current().width); // the 3x5 second upload
    try testing.expectEqual(@as(usize, 5), session.current().height);
    try testing.expectEqual(@as(usize, 0), steps.items.len); // §1 accumulation reset
    try testing.expectEqual(@as(?usize, 2), active);

    // Switching back to the first upload is just as available (the list is not consumed).
    try testing.expect(applyPlanAction(&session, threaded.io(), .{ .image = .{ .index = 1 } }, &edited, &steps, &active));
    try testing.expectEqual(@as(usize, 6), session.current().width);
}

test "plan image op: an index the turn cannot satisfy costs the action, not the plan (§2.1)" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = try attachedSession(a);
    defer session.deinit();

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    var active: ?usize = null;

    // true = keep going: the actions after it still run (contract §2.1).
    try testing.expect(applyPlanAction(&session, threaded.io(), .{ .image = .{ .index = 3 } }, &edited, &steps, &active));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "skipped switching to attached image 3") != null);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "uploaded 2 image(s)") != null);
    try testing.expectEqual(@as(?usize, null), active);
    try testing.expectEqual(@as(usize, 4), session.current().width); // the working image stands
}

test "plan save op writes <name>.stencil, derived from the active upload, suffixed on collision (§2.1)" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = try attachedSession(a);
    defer session.deinit();

    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();
    defer dir.deleteFile(io, "dog.stencil") catch {};
    defer dir.deleteFile(io, "dog 2.stencil") catch {};
    defer dir.deleteFile(io, "kept.stencil") catch {};
    defer dir.deleteFile(io, "test.stencil") catch {};

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);
    var active: ?usize = null;

    // No attachment adopted yet → the working image's own label names the project.
    try testing.expect(applyPlanAction(&session, io, .{ .save = .{ .name = "", .path = "" } }, &edited, &steps, &active));
    try dir.access(io, "test.stencil", .{});

    // After an `image` op the ACTIVE upload names it — directory and extension dropped.
    try testing.expect(applyPlanAction(&session, io, .{ .image = .{ .index = 2 } }, &edited, &steps, &active));
    try testing.expect(applyPlanAction(&session, io, .{ .save = .{ .name = "", .path = "" } }, &edited, &steps, &active));
    try dir.access(io, "dog.stencil", .{});
    // A second save of the same image takes the next free " 2" name, never overwriting.
    try testing.expect(applyPlanAction(&session, io, .{ .save = .{ .name = "", .path = "" } }, &edited, &steps, &active));
    try dir.access(io, "dog 2.stencil", .{});

    // An explicit name wins, and a model-supplied path can never escape the cwd.
    try testing.expect(applyPlanAction(&session, io, .{ .save = .{ .name = "../../kept.png", .path = "" } }, &edited, &steps, &active));
    try dir.access(io, "kept.stencil", .{});

    // The saved bundle is a real project: it re-opens with the switched image's size.
    var reopened = Session{ .gpa = a };
    defer reopened.deinit();
    var proj = try project.loadInto(&reopened, io, "dog.stencil");
    proj.deinit();
    try testing.expectEqual(@as(usize, 3), reopened.current().width);
    try testing.expectEqual(@as(usize, 5), reopened.current().height);
}

test "plan save op with nothing loaded is skipped with a note (§2.1)" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = Session{ .gpa = a }; // no image at all
    defer session.deinit();

    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);
    var active: ?usize = null;
    try testing.expect(applyPlanAction(&session, threaded.io(), .{ .save = .{ .name = "x", .path = "" } }, &edited, &steps, &active));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "skipped save — no working image") != null);
}

test "runPlan: a multi-image image → layout → save plan runs end to end, note-free (§2.1)" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = try attachedSession(a);
    defer session.deinit();

    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();
    defer dir.deleteFile(io, "cat.stencil") catch {};

    // image → layout → save, executed end to end and finished there (§3.0).
    const raw =
        \\{"version":1,"reply":"kept it","actions":[{"op":"image","index":1},
        \\ {"op":"layout","lines":[{"points":[{"x":1,"y":1},{"x":2,"y":1}]}]},
        \\ {"op":"save"}]}
    ;
    try testing.expect(!try runPlan(&session, io, raw, "outline and keep it"));
    const text = cap.text();
    try testing.expect(std.mem.indexOf(u8, text, "note:") == null); // no per-image caveat
    try testing.expectEqual(@as(usize, 6), session.current().width); // attachment 1 adopted
    try dir.access(io, "cat.stencil", .{}); // …and saved under its own name
}

test "runPlan: clearChat defers past the plan's actions; the end-of-turn confirm decides (§10)" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = try testSession(a);
    defer session.deinit();
    session.chat_on = true;
    try session.appendChatTurn(.user, "hello");
    try session.appendChatTurn(.assistant, "hi");

    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const raw = "{\"version\":1,\"reply\":\"ok\",\"actions\":[{\"op\":\"clearChat\"},{\"op\":\"filter\",\"mode\":\"bw\"}]}";

    // A scripted in-app confirm (what the console wires to the TTY/piped prompt).
    const Scripted = struct {
        var answer: bool = false;
        var asked: usize = 0;
        fn confirm(_: ?*anyopaque, _: []const u8) bool {
            asked += 1;
            return answer;
        }
    };
    session.confirm_fn = Scripted.confirm;

    // clearChat only ARMS the deferral: the later filter still executes, the history
    // survives runPlan, and no confirm has been shown yet.
    try testing.expect(!try runPlan(&session, io, raw, "clear our chat and make it b&w"));
    try testing.expect(session.pending_chat_clear);
    try testing.expectEqual(@as(usize, 0), Scripted.asked);
    try testing.expect(session.chat_history.items.len != 0);
    try testing.expect(std.mem.eql(u8, session.state().filter_mode, "bw"));

    // Declined at end of turn: a "clear canceled" note, the history intact.
    finishChatClear(&session);
    try testing.expectEqual(@as(usize, 1), Scripted.asked);
    try testing.expect(!session.pending_chat_clear);
    try testing.expect(session.chat_history.items.len != 0);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "clear canceled") != null);

    // Accepted: the exact /chat clear path — history dropped, /chat clear's own message.
    Scripted.answer = true;
    try testing.expect(!try runPlan(&session, io, raw, "again"));
    finishChatClear(&session);
    try testing.expectEqual(@as(usize, 2), Scripted.asked);
    try testing.expectEqual(@as(usize, 0), session.chat_history.items.len);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "chat history cleared") != null);

    // Nothing pending → the confirm never re-fires; no way to ask (null fn) → declined.
    finishChatClear(&session);
    try testing.expectEqual(@as(usize, 2), Scripted.asked);
    session.pending_chat_clear = true;
    session.confirm_fn = null;
    finishChatClear(&session);
    try testing.expectEqual(@as(usize, 0), session.chat_history.items.len);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "clear canceled") != null);
}

test "runPlan: a variant carrying a misplaced op is dropped; the rest of the plan runs (§1)" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = try testSession(a);
    defer session.deinit();

    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();
    defer dir.deleteFile(io, "variant-sepia.png") catch {};
    const raw =
        \\{"version":1,"reply":"two takes","actions":[{"op":"filter","mode":"bw"}],"variants":[
        \\ {"label":"Wiped","actions":[{"op":"filter","mode":"invert"},{"op":"clear"}]},
        \\ {"label":"Sepia","actions":[{"op":"filter","mode":"sepia"}]}]}
    ;
    try testing.expect(!try runPlan(&session, io, raw, "two takes please"));

    const text = cap.text();
    try testing.expect(std.mem.indexOf(u8, text, "Dropped variant 1 (\"Wiped\")") != null);
    try testing.expect(std.mem.indexOf(u8, text, "\"clear\" can't run inside a variant") != null);
    try testing.expect(std.mem.indexOf(u8, text, "error:") == null); // never a plan failure
    try testing.expect(std.mem.eql(u8, session.state().filter_mode, "bw")); // top level ran
    try testing.expect(session.hasImage()); // the misplaced clear never executed
    try dir.access(io, "variant-sepia.png", .{}); // …and the well-formed variant rendered
    try testing.expectError(error.FileNotFound, dir.access(io, "variant-wiped.png", .{}));
}

// ── Console-settings ops (the §10-analog profile; transport-free like the rest) ──

/// A live-looking server.Client that never touched the network — enough for the
/// resolution/context paths, which only ever read `base` (and must never read `token`).
fn stubClient(a: std.mem.Allocator, io: std.Io, base: []const u8, token: []const u8) !server.Client {
    return .{
        .gpa = a,
        .io = io,
        .base = try a.dupe(u8, base),
        .token = try a.dupe(u8, token),
        .auth = try a.dupe(u8, "Bearer stub-token"),
        .credential = try a.dupe(u8, ""),
    };
}

test "plan accent op recolours the console theme through the /theme path" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = try testSession(a);
    defer session.deinit();

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);
    const color = try a.dupe(u8, "#12ab34"); // arena-owned in a real plan
    defer a.free(color);
    try testing.expect(applyOne(&session, .{ .accent = .{ .color = color, .preset = "" } }, &edited, &steps));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "theme set to #12ab34") != null);
    try testing.expect(!edited); // a console setting, not an image edit — nothing to sync
    handlers.doTheme(&session, "default"); // leave the shared accent as other tests expect it
}

test "plan connect/disconnect resolve only the user's own servers; misses are notes (§10 stance)" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = try testSession(a);
    defer session.deinit();
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();

    // This session connected to two alpha ports; only :8090 is still live.
    try session.rememberServer("http://alpha.example:8090");
    try session.rememberServer("http://alpha.example:9091");
    try session.servers.append(a, try stubClient(a, io, "http://alpha.example:8090", "sekrit-token"));

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);
    var active: ?usize = null;

    // A resolved connect to a live server is a no-op note — no fresh handshake.
    const live = try a.dupe(u8, "http://alpha.example:8090");
    defer a.free(live);
    try testing.expect(applyPlanAction(&session, io, .{ .connect = .{ .server = live } }, &edited, &steps, &active));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "already connected to http://alpha.example:8090") != null);

    // A bare host both known entries share is ambiguous; an unknown host is the user's
    // to /connect — both are notes, and the plan (returns true) carries on.
    try testing.expect(applyPlanAction(&session, io, .{ .connect = .{ .server = "alpha.example" } }, &edited, &steps, &active));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "matches several of this session's servers") != null);
    try testing.expect(applyPlanAction(&session, io, .{ .connect = .{ .server = "http://evil.example" } }, &edited, &steps, &active));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "not a server you connected this session; run '/connect <url>' yourself") != null);
    try testing.expectEqual(@as(usize, 1), session.servers.items.len); // nothing new appeared

    // Disconnect resolves against LIVE connections: the bare host is unique there.
    try testing.expect(applyPlanAction(&session, io, .{ .disconnect = .{ .server = "alpha.example" } }, &edited, &steps, &active));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "disconnected from http://alpha.example:8090") != null);
    try testing.expectEqual(@as(usize, 0), session.servers.items.len);
    // …and a second disconnect finds nothing to drop — a note, not a failure.
    try testing.expect(applyPlanAction(&session, io, .{ .disconnect = .{ .server = "alpha.example" } }, &edited, &steps, &active));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "skipped disconnect — not connected to \"alpha.example\"") != null);
    // The known pool survives the disconnect: the user could ask to reconnect later.
    try testing.expectEqual(@as(usize, 2), session.known_servers.items.len);
    try testing.expect(!edited);
}

test "plan delete op keeps /delete's confirmless semantics AND its guards" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = try testSession(a);
    defer session.deinit();
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();

    try dir.writeFile(io, .{ .sub_path = "plan-delete.stencil", .data = "x" });
    defer dir.deleteFile(io, "plan-delete.stencil") catch {};

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);
    var active: ?usize = null;

    // The console's /delete asks no confirmation, so neither does the op — same command.
    try testing.expect(applyPlanAction(&session, io, .{ .delete = .{ .path = "plan-delete.stencil" } }, &edited, &steps, &active));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "deleted plan-delete.stencil") != null);
    try testing.expectError(error.FileNotFound, dir.access(io, "plan-delete.stencil", .{}));

    // The /delete guards hold: traversal, URLs and non-.stencil paths are refused with
    // the command's own messages — and none of them fails the plan.
    try testing.expect(applyPlanAction(&session, io, .{ .delete = .{ .path = "../up.stencil" } }, &edited, &steps, &active));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "refusing to delete a path that escapes the working directory") != null);
    try testing.expect(applyPlanAction(&session, io, .{ .delete = .{ .path = "https://x/a.stencil" } }, &edited, &steps, &active));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "only removes local files, not URLs") != null);
    try testing.expect(applyPlanAction(&session, io, .{ .delete = .{ .path = "notes.txt" } }, &edited, &steps, &active));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "only removes .stencil project files") != null);
    try testing.expect(!edited);
}

test "plan openUrl loads via the /upload path; later actions see the fetched picture (§10)" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = try testSession(a); // 4x4 — the load below must replace it
    defer session.deinit();
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();

    // A 2x2 red PNG on disk — the loader takes local paths through the same seam
    // /upload uses, so the executor is exercised without any network.
    const px = try a.alloc(u8, 2 * 2 * 4);
    defer a.free(px);
    for (0..4) |i| {
        px[i * 4] = 255;
        px[i * 4 + 1] = 0;
        px[i * 4 + 2] = 0;
        px[i * 4 + 3] = 255;
    }
    const png = try image.encode(a, .{ .width = 2, .height = 2, .pixels = px }, .png);
    defer a.free(png);
    try dir.writeFile(io, .{ .sub_path = "plan-openurl.png", .data = png });
    defer dir.deleteFile(io, "plan-openurl.png") catch {};

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);
    var active: ?usize = 3; // a stale §2.1 pick the load must displace
    try steps.append(a, .{ .crop = .{ .x = 1, .y = 1 } }); // pre-load frame step

    const url = try a.dupe(u8, "plan-openurl.png"); // arena-owned in a real plan
    defer a.free(url);
    try testing.expect(applyPlanAction(&session, io, .{ .open_url = .{ .url = url, .incognito = true } }, &edited, &steps, &active));
    // incognito is not a console concept — noted, load proceeds in place.
    try testing.expect(std.mem.indexOf(u8, cap.text(), "incognito is not a console concept") != null);
    try testing.expectEqual(@as(usize, 2), session.current().width); // the fetched picture
    try testing.expectEqual(@as(usize, 0), steps.items.len); // fresh picture = fresh frame
    try testing.expect(active == null); // an unnamed save now derives from the URL label
    try testing.expectEqualStrings("plan-openurl.png", session.label.?);
    try testing.expect(edited);

    // Synchronous: a following filter acts on the fetched image, not the old 4x4.
    try testing.expect(applyPlanAction(&session, io, .{ .filter = .{ .mode = .bw, .tint = "" } }, &edited, &steps, &active));
    const out = session.current().*;
    try testing.expectEqual(@as(usize, 2), out.width);
    try testing.expect(out.pixels[0] == out.pixels[1] and out.pixels[1] == out.pixels[2]); // red went gray
}

test "plan copy needs a working image — a note + skip, never a stop (§10)" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = Session{ .gpa = a }; // no image loaded
    defer session.deinit();

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);
    try testing.expect(applyOne(&session, .copy, &edited, &steps));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "skipped copy — no working image to copy") != null);
    try testing.expect(!edited);
}

test "runPlan: an openUrl the user never typed fails the whole plan (§10 user-echo guard)" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = try testSession(a);
    defer session.deinit();
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();

    // The model introduced a host the user never wrote: the plan fails, nothing runs —
    // the filter after the openUrl never acknowledges.
    const raw = "{\"reply\":\"on it\",\"actions\":[{\"op\":\"openUrl\",\"url\":\"https://evil.example/x.png\"},{\"op\":\"filter\",\"mode\":\"bw\"}]}";
    try testing.expect(!try runPlan(&session, threaded.io(), raw, "make my picture b&w"));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "openUrl blocked: \"https://evil.example/x.png\" is not a URL you gave in this conversation") != null);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "bw") == null);

    // A USER turn of the /chat conversation counts as an echo (the pool §10 names).
    session.chat_on = true;
    try session.appendChatTurn(.user, "get https://ok.example/cat.png");
    try session.appendChatTurn(.assistant, "see https://evil.example/x.png");
    const hist: []const llm.Turn = session.chat_history.items;
    try testing.expect(llm.urlEchoedByUser(hist, "crop it", "https://ok.example/cat.png"));
    try testing.expect(!llm.urlEchoedByUser(hist, "crop it", "https://evil.example/x.png"));
}

test "runPlan: a load + pixel-free edits requests the §7 continuation; a drawn layout does not" {
    const a = testing.allocator;
    var quiet: u8 = 0;
    logo.setSink(swallowPrint, &quiet);
    defer logo.clearSink();
    var session = try testSession(a);
    defer session.deinit();
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();

    // blank + filter: the model has not seen the loaded picture and drew nothing on it
    // — the caller should re-send the turn once (§7's amended mixed-plan clause).
    const mixed = "{\"reply\":\"x\",\"actions\":[{\"op\":\"blank\",\"color\":\"#ffffff\"},{\"op\":\"filter\",\"mode\":\"bw\"}]}";
    try testing.expect(try runPlan(&session, threaded.io(), mixed, "blank then bw"));

    // blank + layout: the plan committed to coordinates — no continuation.
    const traced = "{\"reply\":\"x\",\"actions\":[{\"op\":\"blank\",\"color\":\"#ffffff\"}," ++
        "{\"op\":\"layout\",\"lines\":[{\"points\":[{\"x\":0,\"y\":0},{\"x\":1,\"y\":1}]}]}]}";
    try testing.expect(!try runPlan(&session, threaded.io(), traced, "blank then draw"));
}

test "console context suffix: URLs + active project ride, the token never does" {
    const a = testing.allocator;
    var quiet: u8 = 0;
    logo.setSink(swallowPrint, &quiet);
    defer logo.clearSink();
    var session = try testSession(a);
    defer session.deinit();
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();

    try session.servers.append(a, try stubClient(a, threaded.io(), "http://ctx.example:8090", "sekrit-token"));
    try session.rememberServer("http://ctx.example:8090");
    try session.setRemote("http://ctx.example:8090", "p_1");
    try session.setLabel("portrait");

    var arena = std.heap.ArenaAllocator.init(a);
    defer arena.deinit();
    // with_projects=false — the offline half; the fetch only ever ADDS project names.
    const ctx = try consoleContext(&session, arena.allocator(), false);
    try testing.expect(std.mem.indexOf(u8, ctx, "http://ctx.example:8090 (active project's server)") != null);
    try testing.expect(std.mem.indexOf(u8, ctx, "Active server project: \"portrait\".") != null);
    try testing.expect(std.mem.indexOf(u8, ctx, "Projects on") == null); // not fetched ≠ empty
    try testing.expect(std.mem.indexOf(u8, ctx, "sekrit-token") == null); // never a token
    try testing.expect(std.mem.indexOf(u8, ctx, "Bearer") == null);
}

// ── The expanded console op set (§2 undo/redo/reset + custom dims, §10 clear/reconnect/…) ──

test "plan undo/redo step the session's own history; running out of steps is a note (§2)" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = try testSession(a); // 4x4
    defer session.deinit();

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);

    // Two REAL session edits to step back through: crop to 3x4, then rotate to 4x3.
    try testing.expect(applyOne(&session, .{ .crop = .{ .x1 = "1px", .y1 = "0px" } }, &edited, &steps));
    try testing.expect(applyOne(&session, .{ .rotate = .{ .dir = .right, .times = 1 } }, &edited, &steps));
    try testing.expectEqual(@as(usize, 4), session.current().width);
    try testing.expectEqual(@as(usize, 3), session.current().height);

    // One undo steps one HISTORY entry back (the rotate), one redo re-applies it.
    try testing.expect(applyOne(&session, .{ .undo = .{ .steps = 1 } }, &edited, &steps));
    try testing.expectEqual(@as(usize, 3), session.current().width);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "undone") != null);
    try testing.expect(applyOne(&session, .{ .redo = .{ .steps = 1 } }, &edited, &steps));
    try testing.expectEqual(@as(usize, 4), session.current().width);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "redone") != null);

    // Asking for more steps than exist takes what is there and notes the shortfall (§2).
    try testing.expect(applyOne(&session, .{ .undo = .{ .steps = 20 } }, &edited, &steps));
    try testing.expectEqual(@as(usize, 4), session.current().width); // back at the original
    try testing.expectEqual(@as(usize, 4), session.current().height);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "only 2 of 20 undo step(s) were available") != null);

    // At the original an undo has nothing to step — the /undo command's own message.
    try testing.expect(applyOne(&session, .{ .undo = .{ .steps = 1 } }, &edited, &steps));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "nothing to undo (at the original)") != null);
}

test "plan reset drops every pending edit back to the original (§2)" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = try testSession(a); // 4x4
    defer session.deinit();

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);
    try testing.expect(applyOne(&session, .{ .crop = .{ .x1 = "1px", .y1 = "0px" } }, &edited, &steps));
    try testing.expect(applyOne(&session, .{ .rotate = .{ .dir = .left, .times = 1 } }, &edited, &steps));

    try testing.expect(applyOne(&session, .reset, &edited, &steps));
    try testing.expectEqual(@as(usize, 4), session.current().width);
    try testing.expectEqual(@as(usize, 4), session.current().height);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "reset to original") != null);
    // The history was dropped, not stepped: there is nothing to redo now.
    try testing.expect(applyOne(&session, .{ .redo = .{ .steps = 1 } }, &edited, &steps));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "nothing to redo (at the latest edit)") != null);
}

test "plan clear drops the working image through the /drop path (§10)" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = try testSession(a);
    defer session.deinit();

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);
    try testing.expect(applyOne(&session, .clear, &edited, &steps));
    try testing.expect(!session.hasImage()); // the editor is empty, not a blank page
    try testing.expect(!edited); // a console-state change, not an image edit to sync
    // Cleared twice is /drop's own note — the plan carries on.
    try testing.expect(applyOne(&session, .clear, &edited, &steps));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "no image loaded") != null);
}

test "plan crop album derives the missing axis from the page, landscape (§10)" {
    const a = testing.allocator;
    var quiet: u8 = 0;
    logo.setSink(swallowPrint, &quiet);
    defer logo.clearSink();

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);

    // x edges given, y missing → album derives the height from the LANDSCAPE page
    // aspect (A4 → 29.7/21 ≈ 1.414): 4px wide → round(4 / 1.414) = 3 high.
    var album_session = try testSession(a); // 4x4
    defer album_session.deinit();
    try testing.expect(applyOne(&album_session, .{ .crop = .{ .x1 = "0px", .x2 = "4px", .album = true } }, &edited, &steps));
    try testing.expectEqual(@as(usize, 4), album_session.current().width);
    try testing.expectEqual(@as(usize, 3), album_session.current().height);

    // Without album the portrait aspect derives a TALLER height (clamped to the image).
    steps.clearRetainingCapacity();
    var portrait_session = try testSession(a);
    defer portrait_session.deinit();
    try testing.expect(applyOne(&portrait_session, .{ .crop = .{ .x1 = "0px", .x2 = "4px" } }, &edited, &steps));
    try testing.expectEqual(@as(usize, 4), portrait_session.current().height);
}

test "plan custom page and blank cm dims land as the session's custom page (§2)" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = try testSession(a);
    defer session.deinit();

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);

    // The custom page form: the /format custom path (name + cm dims).
    try testing.expect(applyOne(&session, .{ .page = .{ .format = "", .width = 10, .height = 20 } }, &edited, &steps));
    try testing.expectEqualStrings("custom", session.page_size);
    try testing.expectEqual(@as(f64, 10), session.custom_page_w);
    try testing.expectEqual(@as(f64, 20), session.custom_page_h);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "page format set to custom") != null);
    try testing.expect(edited); // rides the saved/synced layout, like /format

    // Blank dims size the page exactly like '/blank <w> <h>' (the shared 96-dpi
    // derivation) and override the format; the page pick follows the dims.
    const expect_px = core.defaultBlankSizePx(10, 5, 96.0);
    try testing.expect(applyOne(&session, .{ .blank = .{ .color = "#ffffff", .format = "a4", .width = 10, .height = 5 } }, &edited, &steps));
    try testing.expectEqual(@as(usize, @intCast(expect_px.w)), session.current().width);
    try testing.expectEqual(@as(usize, @intCast(expect_px.h)), session.current().height);
    try testing.expectEqualStrings("custom", session.page_size);
    try testing.expectEqual(@as(f64, 10), session.custom_page_w);
    try testing.expectEqual(@as(f64, 5), session.custom_page_h);
}

test "plan formula enabled:false restores identity keeping expressions; empty expr clears the axis (§2)" {
    const a = testing.allocator;
    var quiet: u8 = 0;
    logo.setSink(swallowPrint, &quiet);
    defer logo.clearSink();
    var session = try testSession(a);
    defer session.deinit();

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);

    try testing.expect(applyOne(&session, .{ .formula = .{ .axis = 'x', .expr = "x*2" } }, &edited, &steps));
    try testing.expect(session.allow_formulas);
    try testing.expectEqualStrings("x*2", session.formula_x);

    // OFF restores identity but keeps the expression (the /formula off semantics)…
    try testing.expect(applyOne(&session, .{ .formula = .{ .axis = 0, .expr = "", .enabled = false } }, &edited, &steps));
    try testing.expect(!session.allow_formulas);
    try testing.expectEqualStrings("x*2", session.formula_x);
    // …and ON brings it back.
    try testing.expect(applyOne(&session, .{ .formula = .{ .axis = 0, .expr = "", .enabled = true } }, &edited, &steps));
    try testing.expect(session.allow_formulas);

    // An empty expr clears exactly that axis.
    try testing.expect(applyOne(&session, .{ .formula = .{ .axis = 'x', .expr = "" } }, &edited, &steps));
    try testing.expectEqualStrings("", session.formula_x);
}

test "plan accent preset resolves through /theme's name table (§10)" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = try testSession(a);
    defer session.deinit();

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);
    const preset = try a.dupe(u8, "green"); // arena-owned in a real plan
    defer a.free(preset);
    try testing.expect(applyOne(&session, .{ .accent = .{ .color = "", .preset = preset } }, &edited, &steps));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "theme set to green (#047857)") != null);
    // An unknown preset is /theme's own note + skip — the plan carries on.
    try testing.expect(applyOne(&session, .{ .accent = .{ .color = "", .preset = "frobnicate" } }, &edited, &steps));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "unknown theme 'frobnicate'") != null);
    try testing.expect(!edited);
    handlers.doTheme(&session, "default"); // leave the shared accent as other tests expect it
}

test "plan reconnect resolves like connect and takes the /reconnect path (§10)" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = try testSession(a);
    defer session.deinit();
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();

    // The pool a plan reconnect resolves against is the servers /connect-ed this
    // session; one OTHER live connection keeps /reconnect from its no-connections
    // early-out without any network involved.
    try session.rememberServer("http://a.example:1");
    try session.rememberServer("http://a.example:2");
    try session.rememberServer("http://b.example:9");
    try session.servers.append(a, try stubClient(a, io, "http://c.example:1", "tok"));

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);
    var active: ?usize = null;

    // A unique host resolves (connect's rule) and reaches doReconnect — which notes a
    // match that is not currently live, exactly like the typed /reconnect would.
    try testing.expect(applyPlanAction(&session, io, .{ .reconnect = .{ .server = "b.example" } }, &edited, &steps, &active));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "not connected to http://b.example:9") != null);
    // Ambiguous and unknown names are notes, never failed plans (§10 stance).
    try testing.expect(applyPlanAction(&session, io, .{ .reconnect = .{ .server = "a.example" } }, &edited, &steps, &active));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "skipped reconnect — \"a.example\" matches several of this session's servers") != null);
    try testing.expect(applyPlanAction(&session, io, .{ .reconnect = .{ .server = "http://evil.example" } }, &edited, &steps, &active));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "skipped reconnect — \"http://evil.example\" is not a server you connected this session") != null);
    try testing.expectEqual(@as(usize, 1), session.servers.items.len); // nothing new appeared
    try testing.expect(!edited);
}

test "plan layout with an empty lines array removes every drawn line (§4)" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = try testSession(a);
    defer session.deinit();
    try session.addLines("{\"lines\":[{\"points\":[{\"x\":0,\"y\":0},{\"x\":1,\"y\":1}],\"color\":\"#FF0000\"}]}");

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);
    try testing.expect(applyOne(&session, .{ .layout = .{ .lines_json = "[]" } }, &edited, &steps));
    try testing.expectEqualStrings("[]", session.state().lines_json);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "lines removed") != null);
    // Undoable, like the draw it removed.
    try testing.expect(session.undo());
    try testing.expect(std.mem.indexOf(u8, session.state().lines_json, "#FF0000") != null);
}

test "renderVariant: a filter op recolours the picture, not the lines already drawn" {
    const a = testing.allocator;
    var quiet: u8 = 0;
    logo.setSink(swallowPrint, &quiet);
    defer logo.clearSink();
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();

    // A red 16x12 picture with a green dot drawn on it, greyscaled by the variant: the
    // background turns grey, the annotation keeps its colour.
    var session = Session{ .gpa = a };
    defer session.deinit();
    const px = try a.alloc(u8, 16 * 12 * 4);
    core.fillRGBA(px, 16 * 12, .{ .r = 200, .g = 40, .b = 40, .a = 255 });
    try session.loadImage(.{ .width = 16, .height = 12, .pixels = px }, "test", true, .png, null);
    try session.addLines("{\"lines\":[{\"points\":[{\"x\":8,\"y\":6}],\"color\":\"#00FF00\",\"pointSize\":3,\"thickness\":2}]}");

    var actions = [_]llm.Action{.{ .filter = .{ .mode = .bw, .tint = "" } }};
    renderVariant(&session, io, .{ .label = "bw", .actions = &actions }, "bwtest", &.{});

    const dir = std.Io.Dir.cwd();
    defer dir.deleteFile(io, "variant-bwtest.png") catch {};
    const bytes = try dir.readFileAlloc(io, "variant-bwtest.png", a, .limited(1 << 20));
    defer a.free(bytes);
    var out = try image.decode(a, bytes);
    defer out.deinit(a);

    const dot = (6 * out.width + 8) * 4;
    try testing.expect(out.pixels[dot + 1] > 200); // green kept
    try testing.expect(out.pixels[dot] < 60 and out.pixels[dot + 2] < 60);
    const bg = (1 * out.width + 1) * 4;
    try testing.expectEqual(out.pixels[bg], out.pixels[bg + 1]);
    try testing.expectEqual(out.pixels[bg + 1], out.pixels[bg + 2]);
}
