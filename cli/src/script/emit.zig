//! `--script <in.stc> --script-emit <out>`: the script re-written as a runnable script for
//! another surface. The OUTPUT's extension picks the target — `.js`/`.stcjs` javascript,
//! `.py`/`.pystc` python — so no flag names a language twice. Lengths, sources and save
//! targets are emitted AS WRITTEN; the generated file resolves them where it runs.
const std = @import("std");

const report = @import("../report.zig");
const scriptCore = @import("../scriptCore.zig");

const common = @import("emit/common.zig");
const js = @import("emit/js.zig");
const py = @import("emit/py.zig");
const load = @import("load.zig");
const save_mod = @import("save.zig");

pub const Error = error{UnknownEmitTarget} || common.Error;

/// What a backend returns when the target cannot carry a directive, with the span it refused.
pub const Refusal = common.Refusal;

pub const Target = enum {
    js,
    py,

    pub fn display(self: Target) []const u8 {
        return switch (self) {
            .js => js.display,
            .py => py.display,
        };
    }
};

const Extension = struct { ext: []const u8, target: Target };

/// The whole mapping: a suffix the user can write, and what it emits.
pub const EXTENSIONS = [_]Extension{
    .{ .ext = ".js", .target = .js },
    .{ .ext = ".stcjs", .target = .js },
    .{ .ext = ".py", .target = .py },
    .{ .ext = ".pystc", .target = .py },
};

/// The target `path` names, or null when its suffix is not one we emit.
pub fn targetFor(path: []const u8) ?Target {
    for (EXTENSIONS) |e| {
        if (path.len > e.ext.len and std.ascii.endsWithIgnoreCase(path, e.ext)) return e.target;
    }
    return null;
}

/// The emitted text, for a caller that owns where it goes. Returns the refusal's span when
/// the target cannot carry a directive — an unrunnable file is never written.
pub fn renderAlloc(
    gpa: std.mem.Allocator,
    script: scriptCore.Script,
    target: Target,
    label: []const u8,
    bad: *common.Refusal,
) ![]u8 {
    var out: std.Io.Writer.Allocating = .init(gpa);
    errdefer out.deinit();
    switch (target) {
        .js => try js.write(&out.writer, script, label, bad),
        .py => try py.write(&out.writer, script, label, bad),
    }
    return out.toOwnedSlice();
}

pub fn run(
    gpa: std.mem.Allocator,
    io: std.Io,
    in_path: []const u8,
    out_path: []const u8,
    confine_output: bool,
) !void {
    const target = targetFor(out_path) orelse {
        report.err("cannot emit '{s}': name it .js, .stcjs, .py or .pystc\n", .{out_path});
        return Error.UnknownEmitTarget;
    };
    try save_mod.guard(out_path, confine_output);

    const source = try load.readScript(gpa, io, in_path);
    defer gpa.free(source);

    var script = scriptCore.Script.parse(source) catch return load.Error.ScriptUnreadable;
    defer script.deinit();

    const label = load.labelFor(in_path);
    if (load.reportDiagnostics(script, label)) return load.Error.ScriptHasErrors;

    var bad: common.Refusal = .{};
    const text = renderAlloc(gpa, script, target, label, &bad) catch |e| {
        if (e != Error.EmitUnsupported) return e;
        report.err("{s}:{d}:{d}: {s}\n", .{ label, bad.line, bad.col, bad.text() });
        return e;
    };
    defer gpa.free(text);

    std.Io.Dir.cwd().writeFile(io, .{ .sub_path = out_path, .data = text }) catch |e| {
        report.err("could not write {s} ({s})\n", .{ out_path, @errorName(e) });
        return e;
    };
    report.print("wrote {s} ({s})\n", .{ out_path, target.display() });
}

test {
    _ = common;
    _ = js;
    _ = py;
}

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
