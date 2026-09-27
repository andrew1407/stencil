//! `--script-emit`: the output's extension picks the target, and each backend writes the
//! script as that surface runs it — or refuses what the surface cannot honour.
const std = @import("std");
const scriptCore = @import("../../src/script/core.zig");
const emit = @import("../../src/script/emit.zig");
const Error = emit.Error;
const Refusal = emit.Refusal;
const Target = emit.Target;
const renderAlloc = emit.renderAlloc;
const targetFor = emit.targetFor;
const common = @import("../../src/script/emit/common.zig");
const report = @import("../../src/app/report.zig");
const testing = std.testing;

test "the output's extension picks the target, and only these four do" {
    try testing.expectEqual(Target.js, targetFor("shots.stcjs").?);
    try testing.expectEqual(Target.js, targetFor("out/SHOTS.JS").?);
    try testing.expectEqual(Target.py, targetFor("shots.pystc").?);
    try testing.expectEqual(Target.py, targetFor("a/b/shots.py").?);
    try testing.expectEqual(@as(?Target, null), targetFor("shots.rb"));
    try testing.expectEqual(@as(?Target, null), targetFor(".py"));
}

test "javascript carries the facade calls, with every length as written" {
    const gpa = testing.allocator;
    var s = try scriptCore.Script.parse(
        "@source https://e.example/a.png:\n  @use line red, 3px\n  @rect (10%, 10%) (-10%, -10%)\n  @filter sepia\n  @save\n",
    );
    defer s.deinit();

    var bad: common.Refusal = .{};
    const text = try renderAlloc(gpa, s, .js, "a.stc", &bad);
    defer gpa.free(text);

    try testing.expect(std.mem.indexOf(u8, text, "await stencil.load('https://e.example/a.png');") != null);
    try testing.expect(std.mem.indexOf(u8, text, "_pt('10%', '10%'), _pt('-10%', '10%')") != null);
    try testing.expect(std.mem.indexOf(u8, text, "stencil.apply({ filter: 'sepia' });") != null);
    try testing.expect(std.mem.indexOf(u8, text, "await _save('');") != null);
}

test "python loops a directory block over the inputs the runner would open" {
    const gpa = testing.allocator;
    var s = try scriptCore.Script.parse("@source shots/:\n  @crop 5%\n  @save out/\n");
    defer s.deinit();

    var bad: common.Refusal = .{};
    const text = try renderAlloc(gpa, s, .py, "a.stc", &bad);
    defer gpa.free(text);

    try testing.expect(std.mem.indexOf(u8, text, "for path in expand_source(\"shots/\", \"dir\"):") != null);
    try testing.expect(std.mem.indexOf(u8, text, "editor.load(path, source=path)") != null);
    try testing.expect(std.mem.indexOf(u8, text, "editor.crop(\"x1=5% x2=-5% y1=5% y2=-5%\")") != null);
    try testing.expect(std.mem.indexOf(u8, text, "_save(editor, \"out/\", path)") != null);
}

test "a local source is refused for the browser, a @frame for python" {
    const gpa = testing.allocator;
    var local = try scriptCore.Script.parse("@source shots/:\n  @filter bw\n  @save\n");
    defer local.deinit();
    var bad: common.Refusal = .{};
    try testing.expectError(Error.EmitUnsupported, renderAlloc(gpa, local, .js, "a.stc", &bad));
    try testing.expect(std.mem.indexOf(u8, bad.text(), "can only open a URL") != null);

    var framed = try scriptCore.Script.parse("@source clip.mp4:\n  @frame 2\n  @save\n");
    defer framed.deinit();
    var bad2: common.Refusal = .{};
    try testing.expectError(Error.EmitUnsupported, renderAlloc(gpa, framed, .py, "a.stc", &bad2));
    try testing.expect(std.mem.indexOf(u8, bad2.text(), "video decoder") != null);
}

test "an implicit block takes the -i input as its source" {
    const gpa = testing.allocator;
    var s = try scriptCore.Script.parse("@filter bw\n@undo 1\n@save o.png\n");
    defer s.deinit();

    var bad: common.Refusal = .{};
    const text = try renderAlloc(gpa, s, .py, "a.stc", &bad);
    defer gpa.free(text);
    try testing.expect(std.mem.indexOf(u8, text, "editor.load(source, source=source)") != null);
    try testing.expect(std.mem.indexOf(u8, text, "_save(editor, \"o.png\", source)") != null);
}

test "an undone edit emits the rewind-and-replay the lowerer reconciled" {
    const gpa = testing.allocator;
    // Undoing the FIRST of two edits: the lowerer rewinds past both, then replays the survivor.
    var s = try scriptCore.Script.parse(
        "@source https://e.example/a.png:\n  @filter bw\n  @crop 5%\n  @undo 1\n  @save\n",
    );
    defer s.deinit();

    var bad: Refusal = .{};
    const js_text = try renderAlloc(gpa, s, .js, "a.stc", &bad);
    defer gpa.free(js_text);
    try testing.expect(std.mem.indexOf(u8, js_text, "for (let i = 0; i < 2; i += 1) stencil.undo();") != null);

    const py_text = try renderAlloc(gpa, s, .py, "a.stc", &bad);
    defer gpa.free(py_text);
    try testing.expect(std.mem.indexOf(u8, py_text, "for _ in range(2): editor.undo()") != null);
    // A target only ever executes ordinary ops: the redo side never reaches one (§7).
    try testing.expect(std.mem.indexOf(u8, py_text, "editor.redo()") == null);
}

test "a colour filter carries its tint, on both targets" {
    const gpa = testing.allocator;
    var s = try scriptCore.Script.parse("@source https://e.example/a.png:\n  @filter #ff3b30\n  @save\n");
    defer s.deinit();

    var bad: Refusal = .{};
    const js_text = try renderAlloc(gpa, s, .js, "a.stc", &bad);
    defer gpa.free(js_text);
    try testing.expect(std.mem.indexOf(u8, js_text, "filter: 'custom', filterColor: '#ff3b30'") != null);

    const py_text = try renderAlloc(gpa, s, .py, "a.stc", &bad);
    defer gpa.free(py_text);
    try testing.expect(std.mem.indexOf(u8, py_text, "editor.apply_filter(\"#ff3b30\")") != null);
}

test "a layout is fetched in the browser and read from disk in python" {
    const gpa = testing.allocator;
    var remote = try scriptCore.Script.parse(
        "@source https://e.example/a.png:\n  @layout https://e.example/l.json replace\n  @save\n",
    );
    defer remote.deinit();
    var bad: Refusal = .{};
    const js_text = try renderAlloc(gpa, remote, .js, "a.stc", &bad);
    defer gpa.free(js_text);
    try testing.expect(std.mem.indexOf(u8, js_text, "applyLayout(await _layout('https://e.example/l.json'), { mode: 'replace' })") != null);

    var local = try scriptCore.Script.parse("@source https://e.example/a.png:\n  @layout marks.json\n  @save\n");
    defer local.deinit();
    const py_text = try renderAlloc(gpa, local, .py, "a.stc", &bad);
    defer gpa.free(py_text);
    try testing.expect(std.mem.indexOf(u8, py_text, "editor.draw(\"marks.json\", combine=True)") != null);
    // The browser has no filesystem, so the same op is refused rather than emitted.
    var bad2: Refusal = .{};
    try testing.expectError(Error.EmitUnsupported, renderAlloc(gpa, local, .js, "a.stc", &bad2));
    try testing.expect(std.mem.indexOf(u8, bad2.text(), "@layout needs a URL") != null);
}

test "a @frame re-opens the source in the browser, where a video can be loaded" {
    const gpa = testing.allocator;
    var s = try scriptCore.Script.parse("@source https://e.example/clip.mp4:\n  @frame 3\n  @filter bw\n  @save\n");
    defer s.deinit();
    var bad: Refusal = .{};
    const text = try renderAlloc(gpa, s, .js, "a.stc", &bad);
    defer gpa.free(text);
    try testing.expect(std.mem.indexOf(u8, text, "await stencil.load('https://e.example/clip.mp4', { frame: 3 });") != null);
}

test "a quote in a save target is escaped, not closed" {
    const gpa = testing.allocator;
    var s = try scriptCore.Script.parse("@source https://e.example/a.png:\n  @filter bw\n  @save \"it's/\"\n");
    defer s.deinit();

    var bad: Refusal = .{};
    const js_text = try renderAlloc(gpa, s, .js, "a.stc", &bad);
    defer gpa.free(js_text);
    try testing.expect(std.mem.indexOf(u8, js_text, "await _save('it\\'s/');") != null);

    const py_text = try renderAlloc(gpa, s, .py, "a.stc", &bad);
    defer gpa.free(py_text);
    try testing.expect(std.mem.indexOf(u8, py_text, "_save(editor, \"it's/\", path)") != null);
}

test "the two suffixes of a target emit the same file" {
    const gpa = testing.allocator;
    var s = try scriptCore.Script.parse("@source https://e.example/a.png:\n  @crop 5%\n  @save\n");
    defer s.deinit();

    var bad: Refusal = .{};
    const stcjs = try renderAlloc(gpa, s, targetFor("a.stcjs").?, "a.stc", &bad);
    defer gpa.free(stcjs);
    const plain_js = try renderAlloc(gpa, s, targetFor("a.js").?, "a.stc", &bad);
    defer gpa.free(plain_js);
    try testing.expectEqualStrings(stcjs, plain_js);
}

test "--script-emit writes the file its extension named, and says which target it was" {
    const gpa = testing.allocator;
    var threaded = std.Io.Threaded.init(gpa, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();

    const Cap = struct {
        var buf: [256]u8 = undefined;
        var len: usize = 0;
        fn take(_: *anyopaque, _: report.Severity, text: []const u8) void {
            len = @min(text.len, buf.len);
            @memcpy(buf[0..len], text[0..len]);
        }
    };
    var unused: u8 = 0;
    report.install(.{ .ctx = @ptrCast(&unused), .emitFn = Cap.take });
    defer report.uninstall();

    const in_path = "stencil_emit_in.stc";
    const out_path = "stencil_emit_out.pystc";
    try dir.writeFile(io, .{ .sub_path = in_path, .data = "@filter bw\n@save o.png\n" });
    defer dir.deleteFile(io, in_path) catch {};
    defer dir.deleteFile(io, out_path) catch {};

    try emit.run(gpa, io, in_path, out_path, false);
    try testing.expect(std.mem.indexOf(u8, Cap.buf[0..Cap.len], "(python)") != null);

    const written = try dir.readFileAlloc(io, out_path, gpa, .limited(1 << 20));
    defer gpa.free(written);
    try testing.expect(std.mem.indexOf(u8, written, "editor.apply_filter(\"bw\")") != null);
    try testing.expect(std.mem.indexOf(u8, written, "from pystencil import Editor") != null);
}

test "an output whose extension names no target is refused before anything is read" {
    const gpa = testing.allocator;
    var threaded = std.Io.Threaded.init(gpa, .{});
    defer threaded.deinit();
    try testing.expectError(
        emit.Error.UnknownEmitTarget,
        emit.run(gpa, threaded.io(), "no-such.stc", "out.rb", false),
    );
}
