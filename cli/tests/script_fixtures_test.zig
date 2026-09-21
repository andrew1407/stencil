//! Walks the shared .stc corpus (script/fixtures/cases.txt) through the cli's own core
//! bridge, the same file core/, browser/, pystencil/ and vscode-extension/ replay. Plain
//! text, not JSON, so the splitting lives here: a section ends at the next marker and the
//! blank line before it belongs to the file.
const std = @import("std");
const fx = @import("fixture_corpus.zig");
const emit = @import("../src/script/emit.zig");
const load = @import("../src/script/load.zig");
const scriptCore = @import("../src/script/core.zig");
const testing = std.testing;

const corpus_path = "script/fixtures/cases.txt";

const Section = enum { script, dump, diagnostics };

const sections = std.StaticStringMap(Section).initComptime(.{
    .{ "script", .script },
    .{ "dump", .dump },
    .{ "diagnostics", .diagnostics },
});

const Case = struct {
    name: []const u8 = "",
    script: []const u8 = "",
    dump: []const u8 = "",
    diagnostics: []const u8 = "",
};

fn joinBody(a: std.mem.Allocator, body: []const []const u8) ![]u8 {
    var n = body.len;
    while (n > 0 and body[n - 1].len == 0) n -= 1;
    var out: std.ArrayList(u8) = .empty;
    for (body[0..n]) |line| {
        try out.appendSlice(a, line);
        try out.append(a, '\n');
    }
    return out.toOwnedSlice(a);
}

fn sectionOf(line: []const u8) ?Section {
    if (!std.mem.startsWith(u8, line, "--- ")) return null;
    return sections.get(line[4..]);
}

fn flush(a: std.mem.Allocator, cur: *Case, section: ?Section, body: []const []const u8) !void {
    const text = try joinBody(a, body);
    switch (section orelse return) {
        .script => cur.script = text,
        .dump => cur.dump = text,
        .diagnostics => cur.diagnostics = text,
    }
}

fn readCases(a: std.mem.Allocator, bytes: []const u8) ![]Case {
    var cases: std.ArrayList(Case) = .empty;
    var body: std.ArrayList([]const u8) = .empty;
    defer body.deinit(a);
    var section: ?Section = null;
    var cur: Case = .{};
    var is_open = false;

    var it = std.mem.splitScalar(u8, bytes, '\n');
    while (it.next()) |raw| {
        const line = if (std.mem.endsWith(u8, raw, "\r")) raw[0 .. raw.len - 1] else raw;
        if (std.mem.startsWith(u8, line, "=== ")) {
            if (is_open) {
                try flush(a, &cur, section, body.items);
                try cases.append(a, cur);
            }
            cur = .{ .name = line[4..] };
            body.clearRetainingCapacity();
            section = null;
            is_open = true;
            continue;
        }
        if (!is_open) continue;
        if (sectionOf(line)) |k| {
            try flush(a, &cur, section, body.items);
            body.clearRetainingCapacity();
            section = k;
            continue;
        }
        if (section != null) try body.append(a, line);
    }
    if (is_open) {
        try flush(a, &cur, section, body.items);
        try cases.append(a, cur);
    }
    return cases.toOwnedSlice(a);
}

/// "line:col:len: severity: message [CODE]" — the twin of core's dumpDiagnostics, written by
/// the same formatter --script-check uses (load.Style.corpus).
fn diagText(a: std.mem.Allocator, s: scriptCore.Script) ![]u8 {
    var out: std.Io.Writer.Allocating = .init(a);
    try load.writeDiagnostics(&out.writer, s, "", .corpus);
    return out.toOwnedSlice();
}

fn loadCases(w: *fx.Walk) ![]Case {
    const bytes = try fx.readAlloc(w.alloc(), w.io(), corpus_path);
    return readCases(w.alloc(), bytes);
}

test "script corpus: every case matches its recorded dump, diagnostics and err-* naming" {
    var w = fx.Walk.start();
    defer w.stop();
    const cases = try loadCases(&w);
    try testing.expect(cases.len >= 40); // the corpus shrank — a case was deleted?

    for (cases) |c| {
        w.walked += 1;
        var s = scriptCore.Script.parse(c.script) catch {
            w.fail("script '{s}': the core refused to parse it\n", .{c.name});
            continue;
        };
        defer s.deinit();

        if (!std.mem.eql(u8, s.dump(), c.dump))
            w.fail("script '{s}' dump:\n  want: {s}\n  cli:  {s}\n", .{ c.name, c.dump, s.dump() });
        const diags = try diagText(w.alloc(), s);
        if (!std.mem.eql(u8, diags, c.diagnostics))
            w.fail("script '{s}' diagnostics:\n  want: {s}\n  cli:  {s}\n", .{ c.name, c.diagnostics, diags });

        const should_error = std.mem.startsWith(u8, c.name, "err-");
        if (s.hasErrors() != should_error)
            w.fail("script '{s}': error expectation is {}\n", .{ c.name, should_error });
    }
    try w.report("script");
}

test "script corpus: a truncated case parses without a crash" {
    var w = fx.Walk.start();
    defer w.stop();
    const cases = try loadCases(&w);

    var src: []const u8 = "";
    for (cases) |c| {
        if (std.mem.eql(u8, c.name, "tour-crop")) src = c.script;
    }
    try testing.expect(src.len > 0);

    var cut: usize = 0;
    while (cut < src.len) : (cut += 7) {
        var s = try scriptCore.Script.parse(src[0..cut]);
        defer s.deinit();
        _ = s.opCount();
    }
}

test "script corpus: the splitter keeps a case's trailing blank line out of its body" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const cases = try readCases(arena.allocator(),
        \\# header
        \\=== one
        \\--- script
        \\@crop 10%
        \\
        \\=== two
        \\--- script
        \\@filter bw
        \\--- diagnostics
        \\1:1:1: warning: w [W_X]
        \\
    );
    try testing.expectEqual(@as(usize, 2), cases.len);
    try testing.expectEqualStrings("one", cases[0].name);
    try testing.expectEqualStrings("@crop 10%\n", cases[0].script);
    try testing.expectEqualStrings("", cases[0].dump);
    try testing.expectEqualStrings("@filter bw\n", cases[1].script);
    try testing.expectEqualStrings("1:1:1: warning: w [W_X]\n", cases[1].diagnostics);
}

test "script corpus: every clean case emits for both targets, or says why it cannot" {
    var w = fx.Walk.start();
    defer w.stop();

    for (try loadCases(&w)) |c| {
        var s = scriptCore.Script.parse(c.script) catch continue;
        defer s.deinit();
        if (s.hasErrors()) continue; // an err-* case never reaches the emitter
        w.walked += 1;

        for ([_]emit.Target{ .js, .py }) |target| {
            var bad: emit.Refusal = .{};
            const text = emit.renderAlloc(w.alloc(), s, target, c.name, &bad) catch |e| {
                // A refusal is a result: it must name the span it refused.
                if (e == emit.Error.EmitUnsupported and bad.text().len > 0 and bad.line > 0) continue;
                w.fail("script '{s}' -> {s}: {s}\n", .{ c.name, target.display(), @errorName(e) });
                continue;
            };
            if (text.len == 0 or text[text.len - 1] != '\n')
                w.fail("script '{s}' -> {s}: the emitted file is empty or unterminated\n", .{ c.name, target.display() });
        }
    }
    try w.report("script emit");
}
