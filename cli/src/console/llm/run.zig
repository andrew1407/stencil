//! `/prompt` — one assistant turn end to end: assemble the request (§4), wait on the
//! transport with the spinner, then hand the reply to plan.zig. The turn owns its upload
//! set and its deferred §10 chat clear.
const std = @import("std");
const image = @import("../../image.zig");
const logo = @import("../../logo.zig");
const llm = @import("../../llm.zig");
const Session = @import("../session.zig").Session;
const handlers = @import("../handlers.zig");
const spinner = @import("../spinner.zig");
const config = @import("config.zig");
const attach = @import("attach.zig");
const plan = @import("plan.zig");
const resolveServerAuth = config.resolveServerAuth;
const consoleContext = config.consoleContext;
const promptImageB64 = attach.promptImageB64;
const attachmentB64 = attach.attachmentB64;
const edgeMapB64 = attach.edgeMapB64;
const runPlan = plan.runPlan;

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
    // Contract §11.4: "2" or "1,3" answers the last ask card by number, expanded to the option LABELS
    // every other surface sends. Anything else is an ordinary prompt and simply drops the card.
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

    // §7 auto-continuation: a plan that LOADED a picture but drew no layout could not have traced it
    // (the attachment rode along before it existed) — re-send the turn ONCE with the new image in hand.
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
        // §7 edge map: the working snapshot through the core contour filter rides as a SECOND image,
        // current turn only. Dropped with the snapshot, or alone when only it is over the cap.
        var edge_b64: ?[]u8 = null;
        defer if (edge_b64) |e| gpa.free(e);
        if (b64 != null) edge_b64 = try edgeMapB64(session, llm.max_image_bytes);

        var imgs: std.ArrayList([]const u8) = .empty;
        defer imgs.deinit(gpa);
        if (b64) |w| try imgs.append(gpa, w);
        if (edge_b64) |e| try imgs.append(gpa, e);
        // §2.1: this turn's uploads ride after the working snapshot in upload order — what an `image` op
        // indexes. One that cannot attach drops the whole set rather than shifting that numbering.
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

        // What is happening in the user's terms, not the wire's: the assistant is thinking, roughly how
        // long that takes, and that Ctrl-C ends it. The line goes when the reply or the error lands.
        var spin = spinner.Spinner{};
        spin.start(if (session.cancel_poll != null)
            "thinking… this can take a minute (Ctrl-C to cancel)"
        else
            "thinking… this can take a minute");
        defer spin.stop(); // every exit: the pre-print hook must not outlive this frame

        // With /chat on the saved conversation is replayed (text-only, §7/§12) before the current turn;
        // off, the request is byte-for-byte the plain single-turn one. The system prompt is §4 + settings.
        const history: []const llm.Turn = if (session.chat_on) session.chat_history.items else &.{};
        var req = try llm.buildRequestWithSystem(gpa, cfg, llm.consoleSystemPrompt(), turn_text, imgs.items, server_url, server_token, history, suffix);
        defer req.deinit(gpa);
        const posted = llm.postJson(gpa, io, req.url, req.auth, req.body, promptWaiter(session, &spin));
        spin.stop();
        const body = posted catch return; // message printed
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

/// The §10 clearChat deferral, run once the WHOLE turn (continuation included) settled: confirm,
/// then take the /chat clear path. Declining is a "clear canceled" note, never a failed plan.
pub fn finishChatClear(session: *Session) void {
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

fn promptWaiter(session: *Session, spin: *spinner.Spinner) llm.Waiter {
    if (session.cancel_poll == null or session.cancel_ctx == null) return .{};
    return .{ .ctx = session.cancel_ctx, .poll = session.cancel_poll, .beat_ctx = spin, .beat = spinBeat };
}

fn spinBeat(ctx: *anyopaque, now_ms: i64) void {
    const spin: *spinner.Spinner = @ptrCast(@alignCast(ctx));
    spin.beat(now_ms);
}
