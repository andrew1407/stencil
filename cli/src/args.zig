//! Command-line parsing for the stencil CLI. Produces an Options struct; the pipeline
//! interprets it. The grammar is intentionally small and order-independent.
const std = @import("std");
const core = @import("core.zig");

pub const Blank = struct {
    width: ?u32 = null,
    height: ?u32 = null,
    color: []const u8 = "white",
};

pub const Options = struct {
    help: bool = false,
    input: ?[]const u8 = null,
    frame: u32 = 0,
    blank: ?Blank = null,
    crop: ?[]const u8 = null,
    album: bool = false,
    rotate: i32 = 0,
    layout: ?[]const u8 = null,
    filter: ?[]const u8 = null,
    output: ?[]const u8 = null,
};

pub const Error = error{
    MissingValue,
    BadNumber,
    UnknownFlag,
    DuplicateSource,
};

const ParseState = struct {
    argv: []const [:0]const u8,
    i: usize,

    fn next(self: *ParseState) ?[:0]const u8 {
        if (self.i >= self.argv.len) return null;
        const v = self.argv[self.i];
        self.i += 1;
        return v;
    }
};

fn value(st: *ParseState, flag: []const u8) Error![:0]const u8 {
    return st.next() orelse {
        std.debug.print("error: {s} expects a value\n", .{flag});
        return Error.MissingValue;
    };
}

fn parseU32(s: []const u8) Error!u32 {
    return std.fmt.parseInt(u32, s, 10) catch return Error.BadNumber;
}

fn parseI32(s: []const u8) Error!i32 {
    return std.fmt.parseInt(i32, s, 10) catch return Error.BadNumber;
}

/// Parse argv (excluding argv[0]). `allocator` is used only to probe colour tokens.
pub fn parse(allocator: std.mem.Allocator, argv: []const [:0]const u8) Error!Options {
    var opts = Options{};
    var st = ParseState{ .argv = argv, .i = 0 };

    while (st.next()) |arg| {
        if (eq(arg, "-h") or eq(arg, "--help")) {
            opts.help = true;
        } else if (eq(arg, "-i") or eq(arg, "--input")) {
            if (opts.blank != null) return Error.DuplicateSource;
            opts.input = try value(&st, "--input");
        } else if (eq(arg, "-f") or eq(arg, "--frame")) {
            opts.frame = try parseU32(try value(&st, "--frame"));
        } else if (eq(arg, "--blank")) {
            if (opts.input != null) return Error.DuplicateSource;
            opts.blank = try parseBlank(allocator, &st);
        } else if (eq(arg, "-c") or eq(arg, "--crop")) {
            opts.crop = try value(&st, "--crop");
        } else if (eq(arg, "--album")) {
            opts.album = true;
        } else if (eq(arg, "-r") or eq(arg, "--rotate")) {
            opts.rotate = try parseI32(try value(&st, "--rotate"));
        } else if (eq(arg, "-l") or eq(arg, "--layout")) {
            opts.layout = try value(&st, "--layout");
        } else if (eq(arg, "--filter")) {
            opts.filter = try value(&st, "--filter");
        } else if (arg.len > 1 and arg[0] == '-' and !looksNegativeNumber(arg)) {
            std.debug.print("error: unknown flag '{s}'\n", .{arg});
            return Error.UnknownFlag;
        } else {
            // A positional argument is the output path (last one wins).
            opts.output = arg;
        }
    }
    return opts;
}

// --blank consumes an optional `width height` pair (both must be integers; omit to use
// the default page size) followed by an optional colour. Tokens are only consumed when
// they match, so `--blank out.png`, `--blank red out.png` and `--blank 800 600 red out`
// all parse without swallowing the output path.
fn parseBlank(allocator: std.mem.Allocator, st: *ParseState) Error!Blank {
    var b = Blank{};
    if (peekU32(st)) |w| {
        b.width = w;
        st.i += 1;
        // A width is only meaningful with a height; require the pair together.
        b.height = peekU32(st) orelse return Error.BadNumber;
        st.i += 1;
    }
    if (st.i < st.argv.len) {
        const peek = st.argv[st.i];
        if (!(peek.len > 0 and peek[0] == '-') and core.parseColor(allocator, peek) != null) {
            b.color = peek;
            st.i += 1;
        }
    }
    return b;
}

fn peekU32(st: *ParseState) ?u32 {
    if (st.i >= st.argv.len) return null;
    return std.fmt.parseInt(u32, st.argv[st.i], 10) catch null;
}

fn eq(a: []const u8, b: []const u8) bool {
    return std.mem.eql(u8, a, b);
}

// "-1", "-90" etc. are values, not flags (so they aren't misread as unknown flags when
// they appear as a stray positional — rotate values are consumed positionally above).
fn looksNegativeNumber(a: []const u8) bool {
    if (a.len < 2 or a[0] != '-') return false;
    for (a[1..]) |ch| if (!std.ascii.isDigit(ch)) return false;
    return true;
}

const testing = std.testing;

test "parse: flags and positional output" {
    const a = testing.allocator;
    const argv = [_][:0]const u8{ "-i", "in.png", "-r", "-1", "--album", "-c", "x1=10%", "out.png" };
    const o = try parse(a, &argv);
    try testing.expectEqualStrings("in.png", o.input.?);
    try testing.expectEqual(@as(i32, -1), o.rotate);
    try testing.expect(o.album);
    try testing.expectEqualStrings("x1=10%", o.crop.?);
    try testing.expectEqualStrings("out.png", o.output.?);
}

test "parse: blank optional dims and colour" {
    const a = testing.allocator;
    const a1 = [_][:0]const u8{ "--blank", "800", "600", "red", "out.png" };
    const o1 = try parse(a, &a1);
    try testing.expectEqual(@as(u32, 800), o1.blank.?.width.?);
    try testing.expectEqualStrings("red", o1.blank.?.color);
    try testing.expectEqualStrings("out.png", o1.output.?);

    const a2 = [_][:0]const u8{ "--blank", "out.png" };
    const o2 = try parse(a, &a2);
    try testing.expect(o2.blank.?.width == null);
    try testing.expectEqualStrings("white", o2.blank.?.color);
    try testing.expectEqualStrings("out.png", o2.output.?);
}

test "parse: input and blank are mutually exclusive" {
    const a = testing.allocator;
    const argv = [_][:0]const u8{ "-i", "x.png", "--blank", "10", "10" };
    try testing.expectError(Error.DuplicateSource, parse(a, &argv));
}
