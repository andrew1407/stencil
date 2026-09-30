//! What `--script-plan` refuses rather than plan wrong: a `@layout` that does not load, a local
//! one under the bot's url-only surface, a URL source that must be sized and does not fetch, and
//! more lines than one layout carries. Each is an error diagnostic spanning the directive.
const std = @import("std");
const testing = std.testing;

const plan = @import("../../../src/script/plan.zig");
const scriptCore = @import("../../../src/script/core.zig");
const args = @import("../../../src/args.zig");

/// `text` plans no block, and its first diagnostic is `code` on `line`, `len` bytes of directive.
fn refused(text: []const u8, opts: args.Options, code: []const u8, line: i64, len: i64) !void {
    const gpa = testing.allocator;
    var threaded = std.Io.Threaded.init(gpa, .{});
    defer threaded.deinit();
    var s = try scriptCore.Script.parse(text);
    defer s.deinit();
    var out: std.Io.Writer.Allocating = .init(gpa);
    defer out.deinit();
    try testing.expect(!try plan.envelope(gpa, threaded.io(), &out.writer, s, opts, "r.stc"));
    var parsed = try std.json.parseFromSlice(std.json.Value, gpa, out.written(), .{});
    defer parsed.deinit();
    try testing.expectEqual(@as(usize, 0), parsed.value.object.get("blocks").?.array.items.len);
    const d = parsed.value.object.get("diagnostics").?.array.items[0].object;
    try testing.expectEqualStrings("error", d.get("severity").?.string);
    try testing.expectEqualStrings(code, d.get("code").?.string);
    try testing.expectEqual(line, d.get("line").?.integer);
    try testing.expectEqual(len, d.get("len").?.integer);
}

test "a layout that does not load is refused, the reason named" {
    try refused("@source ../common/samples/sample.png:\n  @layout http://127.0.0.1:1/l.json\n", .{}, "E_PLAN_LAYOUT_UNREADABLE", 2, 7);
    try refused("@source ../common/samples/sample.png:\n  @layout tests/fixtures/none.json\n", .{}, "E_PLAN_LAYOUT_UNREADABLE", 2, 7);
}

test "the bot's url-only surface never has a local layout read" {
    const text = "@filter bw\n@layout tests/fixtures/none.json\n";
    try refused(text, .{ .input = "../common/samples/sample.png", .plan_surface = "bot" }, "E_PLAN_LAYOUT_LOCAL", 2, 7);
}

test "a URL source whose lines need its size, and which does not fetch, is refused at the first" {
    try refused("@source http://127.0.0.1:1/a.png:\n  @filter bw\n  @line (0,0) (1,1)\n", .{}, "E_PLAN_SOURCE_UNREADABLE", 3, 5);
}

test "more lines than one layout carries refuse the script, the shape named" {
    const gpa = testing.allocator;
    var text: std.ArrayList(u8) = .empty;
    defer text.deinit(gpa);
    try text.appendSlice(gpa, "@source ../common/samples/sample.png:\n");
    for (0..201) |_| try text.appendSlice(gpa, "  @line (0,0) (1,1)\n");
    var threaded = std.Io.Threaded.init(gpa, .{});
    defer threaded.deinit();
    var s = try scriptCore.Script.parse(text.items);
    defer s.deinit();
    var out: std.Io.Writer.Allocating = .init(gpa);
    defer out.deinit();
    try testing.expect(!try plan.envelope(gpa, threaded.io(), &out.writer, s, .{}, "r.stc"));
    try testing.expect(std.mem.indexOf(u8, out.written(), "\"code\":\"E_PLAN_TOO_MANY_LINES\",\"line\":202,\"col\":3,\"len\":5") != null);
    try testing.expect(std.mem.endsWith(u8, out.written(), "\"blocks\":[]}\n"));
}
