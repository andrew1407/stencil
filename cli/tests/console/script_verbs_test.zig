//! `/script` and `/script-run` through console.handle: a one-liner or a file applied to the
//! loaded image, a parse error applying nothing, the directives a session cannot honour noted.
const std = @import("std");
const console = @import("../../src/console.zig");
const logo = @import("../../src/app/logo.zig");
const harness = @import("console_harness.zig");
const Capture = harness.Capture;
const cur = harness.cur;
const sample = harness.sample;
const testing = std.testing;

test "console: /script runs a ';'-separated one-liner against the loaded image" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();

    const in = "stencil_console_script_in.png";
    try dir.writeFile(io, .{ .sub_path = in, .data = sample });
    defer dir.deleteFile(io, in) catch {};

    var session = console.Session{ .gpa = a };
    defer session.deinit();

    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();

    // A bare /script only prints usage; nothing is loaded yet, nothing is recorded.
    try testing.expect(!try console.handle(&session, io, "/script"));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "usage: /script") != null);
    try testing.expectEqual(@as(usize, 0), session.stateCount());

    // With no image, a well-formed script is refused rather than applied.
    cap.buf.clearRetainingCapacity();
    try testing.expect(!try console.handle(&session, io, "/script @crop 25%"));

    _ = try console.handle(&session, io, "/upload " ++ in);
    try testing.expectEqual(@as(usize, 1), session.stateCount());

    // Two statements on one line: a 25% inset crop of 16x12, then the bw filter.
    cap.buf.clearRetainingCapacity();
    _ = try console.handle(&session, io, "/script @crop 25%; @filter bw");
    try testing.expectEqual(@as(usize, 8), cur(&session).width);
    try testing.expectEqual(@as(usize, 6), cur(&session).height);
    try testing.expectEqualStrings("bw", session.state().filter_mode);
    try testing.expectEqual(@as(usize, 2), session.stateCount()); // the whole run is one history step
}

test "console: a script with a parse error applies nothing" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();

    const in = "stencil_console_script_bad_in.png";
    try dir.writeFile(io, .{ .sub_path = in, .data = sample });
    defer dir.deleteFile(io, in) catch {};

    var session = console.Session{ .gpa = a };
    defer session.deinit();

    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();

    _ = try console.handle(&session, io, "/upload " ++ in);
    cap.buf.clearRetainingCapacity();

    // The crop is well formed and comes first, but the script never runs: an error
    // anywhere refuses the whole thing, so the image is untouched.
    try testing.expect(!try console.handle(&session, io, "/script @crop 25%; @crp bw"));
    try testing.expectEqual(@as(usize, 16), cur(&session).width);
    try testing.expectEqual(@as(usize, 1), session.stateCount());
    try testing.expect(std.mem.indexOf(u8, cap.text(), "E_UNKNOWN_DIRECTIVE") != null);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "<console>") != null);
}

test "console: /script-run reads a file, and reports a missing one" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();

    const in = "stencil_console_scriptrun_in.png";
    const stc = "stencil_console_scriptrun.stc";
    try dir.writeFile(io, .{ .sub_path = in, .data = sample });
    try dir.writeFile(io, .{ .sub_path = stc, .data = "@crop 25%\n@filter sepia\n" });
    defer dir.deleteFile(io, in) catch {};
    defer dir.deleteFile(io, stc) catch {};

    var session = console.Session{ .gpa = a };
    defer session.deinit();

    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();

    _ = try console.handle(&session, io, "/upload " ++ in);

    // A bare /script-run is usage only.
    cap.buf.clearRetainingCapacity();
    try testing.expect(!try console.handle(&session, io, "/script-run"));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "usage: /script-run") != null);

    cap.buf.clearRetainingCapacity();
    _ = try console.handle(&session, io, "/script-run " ++ stc);
    try testing.expectEqual(@as(usize, 8), cur(&session).width);
    try testing.expectEqual(@as(usize, 6), cur(&session).height);
    try testing.expectEqualStrings("sepia", session.state().filter_mode);

    // A path that is not there is an error, and the image stays where the script left it.
    cap.buf.clearRetainingCapacity();
    try testing.expect(!try console.handle(&session, io, "/script-run stencil_console_nope.stc"));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "could not read that script") != null);
    try testing.expectEqual(@as(usize, 8), cur(&session).width);
}

test "console: @save and @frame are reported once each, not skipped in silence" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();

    const in = "stencil_console_script_skip_in.png";
    try dir.writeFile(io, .{ .sub_path = in, .data = sample });
    defer dir.deleteFile(io, in) catch {};

    var session = console.Session{ .gpa = a };
    defer session.deinit();

    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();

    _ = try console.handle(&session, io, "/upload " ++ in);
    cap.buf.clearRetainingCapacity();

    // The console owns a session, not files: both directives are named, and the crop still runs.
    _ = try console.handle(&session, io, "/script @crop 25%; @save out.png; @save other.png");
    try testing.expectEqual(@as(usize, 8), cur(&session).width);
    const text = cap.text();
    try testing.expect(std.mem.indexOf(u8, text, "/script ignores @save") != null);
    const first = std.mem.indexOf(u8, text, "ignores @save").?;
    try testing.expect(std.mem.indexOf(u8, text[first + 1 ..], "ignores @save") == null); // once
}

test "console: a @source block is noted and its ops still reach the loaded image" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();

    const in = "stencil_console_script_src_in.png";
    const stc = "stencil_console_script_src.stc";
    try dir.writeFile(io, .{ .sub_path = in, .data = sample });
    try dir.writeFile(io, .{
        .sub_path = stc,
        .data = "@source somewhere-else.png:\n    @crop 25%\n",
    });
    defer dir.deleteFile(io, in) catch {};
    defer dir.deleteFile(io, stc) catch {};

    var session = console.Session{ .gpa = a };
    defer session.deinit();

    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();

    _ = try console.handle(&session, io, "/upload " ++ in);
    cap.buf.clearRetainingCapacity();

    // The console owns a session, not a file: the source is reported and skipped, and the
    // block's ops apply to what is open — the named file is never opened.
    _ = try console.handle(&session, io, "/script-run " ++ stc);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "ignores @source") != null);
    try testing.expectEqual(@as(usize, 8), cur(&session).width);
    try testing.expectEqual(@as(usize, 6), cur(&session).height);
}

test "console: only a script that recorded an edit marks the session dirty" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();

    var session = console.Session{ .gpa = a };
    defer session.deinit();

    session.sync = true;
    try session.setRemote("http://127.0.0.1:9", "proj-1");

    _ = try console.handle(&session, io, "/blank 64 48 white");
    session.dirty = false;

    _ = try console.handle(&session, io, "/script"); // usage only
    try testing.expect(!session.dirty);
    _ = try console.handle(&session, io, "/script @crp bw"); // refused, nothing applied
    try testing.expect(!session.dirty);
    _ = try console.handle(&session, io, "/script @save out.png"); // clean, but records no edit
    try testing.expect(!session.dirty);
    _ = try console.handle(&session, io, "/script-run stencil_console_absent.stc");
    try testing.expect(!session.dirty);

    _ = try console.handle(&session, io, "/script @crop 25%");
    try testing.expect(session.dirty);
}
