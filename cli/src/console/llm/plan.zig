//! From a model reply to a finished turn: parse + validate the op-plan (§3), run its
//! actions in order, show an `ask` card (§11), and render any §9 variants.
const std = @import("std");
const image = @import("../../media/image.zig");
const logo = @import("../../app/logo.zig");
const llm = @import("../../llm.zig");
const layout_mod = @import("../../media/layout.zig");
const ui = @import("../ui.zig");
const inert = @import("../render/inert.zig");
const Session = @import("../session.zig").Session;
const remoteEvents = @import("../remoteEvents.zig");
const edits = @import("edits.zig");
const settings = @import("settings.zig");
const variants = @import("variants.zig");
const renderVariants = variants.renderVariants;

/// True when the plan loaded a picture it could not yet trace, so the turn goes once more (§7).
pub fn runPlan(session: *Session, io: std.Io, raw: []const u8, user_text: []const u8) !bool {
    return try runPlanOutcome(session, io, raw, user_text) == .again;
}

/// How a reply's plan ended: ran (`done`), ran and wants the §7 continuation (`again`), or was
/// refused or stopped with nothing more to do (`failed`, its message printed).
pub const Outcome = enum { done, again, failed };

pub fn runPlanOutcome(session: *Session, io: std.Io, raw: []const u8, user_text: []const u8) !Outcome {
    const gpa = session.gpa;
    switch (try llm.parsePlan(gpa, raw)) {
        .invalid => |msg| {
            defer gpa.free(msg);
            const said = inert.copy(gpa, msg); // core's words around what the model wrote
            defer said.free(gpa);
            logo.err("{s}\n", .{said.text});
        },
        .plan => |p| {
            var plan = p;
            defer plan.deinit();
            const reply = inert.copy(gpa, plan.reply);
            defer reply.free(gpa);
            logo.print("{s}\n", .{reply.text});
            if (session.chat_on) {
                session.appendChatTurn(.user, user_text) catch {};
                session.appendChatTurn(.assistant, plan.reply) catch {};
            }
            for (plan.warnings) |w| {
                const said = inert.copy(gpa, w);
                defer said.free(gpa);
                logo.note("{s}\n", .{said.text});
            }
            // §11: a plan may also ASK. Printed after the reply and remembered, so the next
            // /prompt can answer it by number. A chat-only turn can carry one too.
            if (plan.ask) |ask| try showAsk(session, ask);
            if (plan.chat_only) return .done;
            // The console's working input is never a video, so a frame op anywhere is a
            // plan-level error: nothing executes (contract §2).
            if (plan.hasFrameOp()) {
                logo.err("the frame op needs a video input — the console session works on a still image\n", .{});
                return .failed;
            }
            // §10 openUrl guard: the model may only ECHO the user — a URL absent from the
            // user's own messages this conversation fails the whole plan, nothing executes.
            const user_turns: []const llm.Turn = if (session.chat_on) session.chat_history.items else &.{};
            for (plan.actions) |a| switch (a) {
                .open_url => |o| if (!llm.urlEchoedByUser(user_turns, user_text, o.url)) {
                    var b: inert.Buf = undefined;
                    logo.err("openUrl blocked: \"{s}\" is not a URL you gave in this conversation\n", .{inert.name(&b, o.url)});
                    return .failed;
                },
                // The assistant may read and write only where the user themselves pointed it, so a plan can never
                // go looking through the disk or drop results somewhere the user never named.
                .open_file => |f| if (!llm.pathEchoedByUser(user_turns, user_text, f.path)) {
                    var b: inert.Buf = undefined;
                    logo.err("openFile blocked: \"{s}\" is not a path you gave in this conversation\n", .{inert.name(&b, f.path)});
                    return .failed;
                },
                .save => |s| if (s.path.len != 0 and !llm.pathEchoedByUser(user_turns, user_text, s.path)) {
                    var b: inert.Buf = undefined;
                    logo.err("save blocked: \"{s}\" is not a path you gave in this conversation\n", .{inert.name(&b, s.path)});
                    return .failed;
                },
                else => {},
            };
            var edited = false;
            // Plan coordinates are in the frame of the §7 snapshot (llm-contract §1), so executed crop/rotate
            // actions accumulate as frame steps and a later layout's points re-map through them.
            var frame_steps: std.ArrayList(layout_mod.FrameStep) = .empty;
            defer frame_steps.deinit(gpa);
            // §2.1: which `/upload`ed attachment the last `image` op adopted (1-based) —
            // an unnamed `save` names its project after it.
            var active: ?usize = null;
            var stopped = false;
            for (plan.actions) |a| {
                if (!applyPlanAction(session, io, a, &edited, &frame_steps, &active)) {
                    stopped = true; // message printed; stop here
                    break;
                }
            }
            if (edited) remoteEvents.markDirty(session); // debounced, flushed at the prompt boundary
            // Variants branch from the post-actions state, so their snapshot-frame
            // coordinates continue through the top-level steps accumulated above.
            renderVariants(session, io, &plan, frame_steps.items);
            if (plan.loadsWithoutTracing() and session.hasImage()) return .again;
            return if (stopped) .failed else .done;
        },
    }
    return .failed;
}

/// Print an `ask` card as a numbered list (§11.4: no previews in a console) and remember the labels
/// so the next /prompt can answer by number. A card replaces any earlier one.
fn showAsk(session: *Session, ask: llm.Ask) !void {
    const gpa = session.gpa;
    session.clearAsk();
    const question = inert.copy(gpa, ask.question);
    defer question.free(gpa);
    logo.print("\n{s}\n", .{question.text});
    var b: inert.Buf = undefined;
    for (ask.options, 0..) |o, i| logo.print("  {d}. {s}\n", .{ i + 1, inert.name(&b, o.label) });
    if (ask.allow_custom) logo.print("  or type your own: {s}\n", .{inert.name(&b, ask.custom_label)});
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

/// Run one validated op-plan action, editing state the caller carries across the plan. False = the
/// action failed; the image-transforming ops need a working image, as their console commands do.
pub fn applyPlanAction(
    session: *Session,
    io: std.Io,
    a: llm.Action,
    edited: *bool,
    frame_steps: *std.ArrayList(layout_mod.FrameStep),
    active: *?usize,
) bool {
    switch (a) {
        .crop, .rotate, .filter, .layout => if (!session.hasImage()) {
            ui.noImage();
            return false;
        },
        else => {},
    }
    return switch (a) {
        .crop, .rotate, .filter, .layout, .formula, .page, .blank, .image, .save => edits.apply(session, io, a, edited, frame_steps, active),
        else => settings.apply(session, io, a, edited, frame_steps, active),
    };
}
