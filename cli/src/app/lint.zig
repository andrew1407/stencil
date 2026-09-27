//! The tree's source lints, walking src/: only the presentation layer paints a terminal (everything
//! below reports through report.zig), the severity prefixes have one definition, and no file keeps
//! a private alias or import nothing reads — Zig's lazy analysis compiles those without a word.
const std = @import("std");
const testing = std.testing;

/// Files that may paint a terminal: this module, the sink in front of it, the entry points,
/// and the two interactive surfaces (see presentation_dirs for their packages).
const presentation = [_][]const u8{
    "main.zig", "args.zig", "console.zig", "project/cli.zig",
};
const presentation_dirs = [_][]const u8{ "app/", "bench/", "console/", "line_edit/", "params/" };

fn isPresentation(rel: []const u8) bool {
    for (presentation) |p| if (std.mem.eql(u8, rel, p)) return true;
    for (presentation_dirs) |d| if (std.mem.startsWith(u8, rel, d)) return true;
    return false;
}

/// The shipped half of a source file: everything before the first column-0 `test`, so an
/// assertion QUOTING a prefix or an escape never counts as a call site.
fn productionPart(src: []const u8) []const u8 {
    var i: usize = 0;
    while (std.mem.indexOfPos(u8, src, i, "\ntest ")) |at| {
        if (src[at + 6] == '"' or src[at + 6] == '{') return src[0 .. at + 1];
        i = at + 1;
    }
    return src;
}

test "layering: severity has one definition, and only the presentation layer prints" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

    // cwd is cli/ under `zig build test`; tolerate a run from the repo root.
    var src_dir = std.Io.Dir.cwd().openDir(io, "src", .{ .iterate = true }) catch
        try std.Io.Dir.cwd().openDir(io, "cli/src", .{ .iterate = true });
    defer src_dir.close(io);

    const literals = [_][]const u8{ "\"error: ", "\"note: ", "\"warning: " };
    const prints = [_][]const u8{ "logo.print(", "logo.err(", "logo.note(", "logo.banner(", "std.debug.print(" };
    const escapes = [_][]const u8{ "\\x1b", "\\x1B", "\\u{1b}", "\\033", "\x1b" };

    var seen: usize = 0;
    var failures: usize = 0;
    var walker = try src_dir.walk(a);
    defer walker.deinit();
    while (try walker.next(io)) |e| {
        if (e.kind != .file or !std.mem.endsWith(u8, e.basename, ".zig")) continue;
        const rel = try a.dupe(u8, e.path);
        std.mem.replaceScalar(u8, rel, '\\', '/'); // walker paths are host-separated
        const prod = productionPart(try e.dir.readFileAlloc(io, e.basename, a, .limited(4 << 20)));
        seen += 1;

        // Every layer: the `error: `/`note: ` wording and colouring live in err()/note().
        if (!std.mem.eql(u8, rel, "app/logo/severity.zig")) {
            for (literals) |lit| if (std.mem.indexOf(u8, prod, lit) != null) {
                std.debug.print("LITERAL PREFIX: {s} spells {s} itself — call err()/note()\n", .{ rel, lit });
                failures += 1;
            };
        }
        if (isPresentation(rel)) continue;

        // Below the line: no terminal at all — report.zig is the only way out.
        for (prints) |call| if (std.mem.indexOf(u8, prod, call) != null) {
            std.debug.print("LAYER BREAK: {s} calls {s} — go through report.zig\n", .{ rel, call });
            failures += 1;
        };
        for (escapes) |esc| if (std.mem.indexOf(u8, prod, esc) != null) {
            std.debug.print("LAYER BREAK: {s} writes an ANSI escape — styling is the console's\n", .{rel});
            failures += 1;
        };
    }
    try testing.expect(seen >= 30); // the tree really was walked
    try testing.expectEqual(@as(usize, 0), failures);
}

/// The name a private top-level one-line declaration on `line` binds — an import, an alias, a
/// constant (`const x = …;`, `const x: T = …;`) — else null. `pub` ones are exports.
fn aliasName(line: []const u8) ?[]const u8 {
    if (!std.mem.startsWith(u8, line, "const ")) return null;
    const rest = line["const ".len..];
    var n: usize = 0;
    while (n < rest.len and isIdent(rest[n])) n += 1;
    if (n == 0 or n == rest.len or (rest[n] != ' ' and rest[n] != ':')) return null;
    if (std.mem.indexOf(u8, rest, " = ") == null) return null;
    if (!std.mem.endsWith(u8, std.mem.trimEnd(u8, rest, " \t\r"), ";") and std.mem.indexOf(u8, rest, "; //") == null) return null;
    return rest[0..n];
}

fn isIdent(ch: u8) bool {
    return std.ascii.isAlphanumeric(ch) or ch == '_';
}

/// Whether `name` appears as a word anywhere outside its own declaration, the line at `decl`.
fn usedElsewhere(src: []const u8, name: []const u8, decl: usize) bool {
    const decl_end = std.mem.indexOfScalarPos(u8, src, decl, '\n') orelse src.len;
    var i: usize = 0;
    while (std.mem.indexOfPos(u8, src, i, name)) |at| : (i = at + 1) {
        if (at >= decl and at < decl_end) continue;
        const before = at == 0 or !isIdent(src[at - 1]);
        const after = at + name.len >= src.len or !isIdent(src[at + name.len]);
        if (before and after and isCode(src, at)) return true;
    }
    return false;
}

/// Whether offset `at` is code: not inside a string literal nor after a `//` on its line.
fn isCode(src: []const u8, at: usize) bool {
    const start = if (std.mem.lastIndexOfScalar(u8, src[0..at], '\n')) |nl| nl + 1 else 0;
    var quoted = false;
    var i = start;
    while (i < at) : (i += 1) {
        if (src[i] == '"' and (i == start or src[i - 1] != '\\')) quoted = !quoted;
        if (!quoted and src[i] == '/' and i + 1 < at and src[i + 1] == '/') return false;
    }
    return !quoted;
}

test "lint: no file keeps a private import, alias or constant that nothing reads" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    var src_dir = std.Io.Dir.cwd().openDir(io, "src", .{ .iterate = true }) catch
        try std.Io.Dir.cwd().openDir(io, "cli/src", .{ .iterate = true });
    defer src_dir.close(io);

    var seen: usize = 0;
    var failures: usize = 0;
    var walker = try src_dir.walk(a);
    defer walker.deinit();
    while (try walker.next(io)) |e| {
        if (e.kind != .file or !std.mem.endsWith(u8, e.basename, ".zig")) continue;
        const rel = try a.dupe(u8, e.path);
        std.mem.replaceScalar(u8, rel, '\\', '/');
        const src = try e.dir.readFileAlloc(io, e.basename, a, .limited(4 << 20));
        seen += 1;
        var off: usize = 0;
        var it = std.mem.splitScalar(u8, src, '\n');
        while (it.next()) |line| : (off += line.len + 1) {
            const name = aliasName(line) orelse continue;
            if (usedElsewhere(src, name, off)) continue;
            std.debug.print("UNUSED: src/{s}: `const {s}` is read nowhere — delete it\n", .{ rel, name });
            failures += 1;
        }
    }
    try testing.expect(seen >= 30);
    try testing.expectEqual(@as(usize, 0), failures);
}

test "lint: what counts as a one-line declaration" {
    try testing.expectEqualStrings("x", aliasName("const x = @import(\"x.zig\");").?);
    try testing.expectEqualStrings("Span", aliasName("const Span = ansi.Span;").?);
    try testing.expect(aliasName("pub const Span = ansi.Span;") == null);
    try testing.expectEqualStrings("max", aliasName("const max = 8;").?); // an unread constant is as dead
    try testing.expectEqualStrings("n", aliasName("const n = f(x); // why").?);
    try testing.expectEqualStrings("k", aliasName("const k: u8 = 3;").?);
    try testing.expect(aliasName("const T = struct {") == null); // a block is not one line
    try testing.expect(aliasName("    const x = a.b;") == null); // not top level
    try testing.expect(usedElsewhere("const ab = x;\nab.y", "ab", 0));
    try testing.expect(!usedElsewhere("const ab = x;\nabc", "ab", 0));
    try testing.expect(!usedElsewhere("const ab = p.ab;\n", "ab", 0)); // its own right-hand side
    try testing.expect(!usedElsewhere("const ab = x;\n// ab, in prose\n", "ab", 0));
    try testing.expect(usedElsewhere("const ab = x;\nf(\"a//b\", ab);\n", "ab", 0));
    try testing.expect(!usedElsewhere("const ab = x;\nf(\"ab\");\n", "ab", 0)); // a string names nothing
}
