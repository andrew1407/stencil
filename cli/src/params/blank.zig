//! `--blank`'s trailing tokens: an optional page-format name (case-insensitive, stored
//! canonical) or a `width height` pair, then an optional colour. A token is consumed only
//! when it matches, so the output path after it is never swallowed.
const std = @import("std");
const testing = std.testing;
const core = @import("../core.zig");
const logo = @import("../app/logo.zig");
const options = @import("options.zig");
const state = @import("state.zig");
const parser = @import("parse.zig");

const Blank = options.Blank;
const Error = options.Error;
const ParseState = state.ParseState;

pub fn parseBlank(st: *ParseState) Error!Blank {
    var b = Blank{};
    if (st.i < st.argv.len) {
        if (core.canonicalPageFormat(st.argv[st.i])) |name| {
            b.page = name;
            st.i += 1;
        }
    }
    if (try peekDim(st)) |w| {
        // A format token names the size, so it excludes explicit dims.
        if (b.page != null) {
            logo.err("--blank takes a page format OR explicit dims, not both\n", .{});
            return Error.BadNumber;
        }
        b.width = w;
        st.i += 1;
        // A width is only meaningful with a height; require the pair together.
        b.height = (try peekDim(st)) orelse {
            logo.err("--blank takes a width and a height, got only '{d}'\n", .{w});
            return Error.BadNumber;
        };
        st.i += 1;
    }
    if (st.i < st.argv.len) {
        const peek = st.argv[st.i];
        if (!(peek.len > 0 and peek[0] == '-') and core.parseColor(peek) != null) {
            b.color = peek;
            st.i += 1;
        }
    }
    return b;
}

/// The next token as a page dimension: null when it is no number (so not a dim), refused when
/// it is one but not a positive pixel count.
fn peekDim(st: *ParseState) Error!?u32 {
    if (st.i >= st.argv.len) return null;
    const tok = st.argv[st.i];
    const n = std.fmt.parseInt(i64, tok, 10) catch return null;
    if (n >= 1 and n <= std.math.maxInt(u32)) return @intCast(n);
    logo.err("--blank expects a width and height of at least 1 px, got '{s}'\n", .{tok});
    return Error.BadNumber;
}

test "parse: blank optional dims and colour" {
    const a1 = [_][:0]const u8{ "--blank", "800", "600", "red", "out.png" };
    const o1 = try parser.parse(&a1);
    try testing.expectEqual(@as(u32, 800), o1.blank.?.width.?);
    try testing.expectEqualStrings("red", o1.blank.?.color);
    try testing.expectEqualStrings("out.png", o1.output.?);

    const a2 = [_][:0]const u8{ "--blank", "out.png" };
    const o2 = try parser.parse(&a2);
    try testing.expect(o2.blank.?.page == null);
    try testing.expect(o2.blank.?.width == null);
    try testing.expectEqualStrings("white", o2.blank.?.color);
    try testing.expectEqualStrings("out.png", o2.output.?);
}

test "parse: blank optional page-format token" {
    // A leading format name (any case) picks the page; the colour still parses after it.
    const a1 = [_][:0]const u8{ "--blank", "b5", "pink", "out.png" };
    const o1 = try parser.parse(&a1);
    try testing.expectEqualStrings("B5", o1.blank.?.page.?);
    try testing.expect(o1.blank.?.width == null);
    try testing.expectEqualStrings("pink", o1.blank.?.color);
    try testing.expectEqualStrings("out.png", o1.output.?);

    const a2 = [_][:0]const u8{ "--blank", "A5", "out.png" };
    const o2 = try parser.parse(&a2);
    try testing.expectEqualStrings("A5", o2.blank.?.page.?);
    try testing.expectEqualStrings("out.png", o2.output.?);

    // A format token and explicit dims are mutually exclusive.
    const a3 = [_][:0]const u8{ "--blank", "b5", "800", "600", "out.png" };
    try testing.expectError(Error.BadNumber, parser.parse(&a3));
}

test "parse: blank dims must be positive, and come as a pair" {
    for ([_][:0]const u8{ "-5", "0" }) |bad| {
        const argv = [_][:0]const u8{ "--blank", bad, bad, "out.png" };
        try testing.expectError(Error.BadNumber, parser.parse(&argv));
    }
    const lone = [_][:0]const u8{ "--blank", "800", "out.png" };
    try testing.expectError(Error.BadNumber, parser.parse(&lone));
}
