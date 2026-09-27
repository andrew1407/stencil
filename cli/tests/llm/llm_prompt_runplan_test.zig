//! A whole plan through runPlan: a multi-image plan end to end, a clearChat deferred past the
//! actions, and a variant's misplaced op dropped while the rest runs.
const std = @import("std");
const logo = @import("../../src/app/logo.zig");
const run = @import("../../src/console/llm/run.zig");
const finishChatClear = run.finishChatClear;
const fixture = @import("../../src/console/llm/fixture.zig");
const Capture = fixture.Capture;
const attachedSession = fixture.attachedSession;
const testSession = fixture.testSession;
const plan = @import("../../src/console/llm/plan.zig");
const runPlan = plan.runPlan;
const testing = std.testing;

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
    try testing.expect(std.mem.indexOf(u8, text, "editor-settings op \"clear\" is not allowed inside variants") != null);
    try testing.expect(std.mem.indexOf(u8, text, "error:") == null); // never a plan failure
    try testing.expect(std.mem.eql(u8, session.state().filter_mode, "bw")); // top level ran
    try testing.expect(session.hasImage()); // the misplaced clear never executed
    try dir.access(io, "variant-sepia.png", .{}); // …and the well-formed variant rendered
    try testing.expectError(error.FileNotFound, dir.access(io, "variant-wiped.png", .{}));
}
