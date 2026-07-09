//! Command-line parsing for the stencil CLI. Produces an Options struct; the pipeline
//! interprets it. The grammar is intentionally small and order-independent.
const std = @import("std");
const core = @import("core.zig");

pub const Blank = struct {
    page: ?[]const u8 = null, // named page format ("A4".."C10", canonical); null = default
    width: ?u32 = null,
    height: ?u32 = null,
    color: []const u8 = "white",
};

pub const Options = struct {
    help: bool = false,
    console: bool = false,
    input: ?[]const u8 = null,
    frame: u32 = 0,
    blank: ?Blank = null,
    crop: ?[]const u8 = null,
    album: bool = false,
    rotate: i32 = 0,
    layout: ?[]const u8 = null,
    filter: ?[]const u8 = null,
    output: ?[]const u8 = null,
    // ── Server (collaboration server) options ──
    // --server <url>: -i names a server project, fetched + edited. --remote-update writes
    // the result back into it. --remote <url> + --remote-name <name>: upload the result as
    // a NEW project (default name = input image name; a web source URL recorded as source).
    server: ?[]const u8 = null,
    remote: ?[]const u8 = null,
    remote_name: ?[]const u8 = null,
    remote_update: bool = false,
    // ── Source-site scraping ──
    // --source-site <url> activates scrape mode (mutually exclusive with -i/--blank/--server):
    // fetch the page, extract + filter media, and download the matches into <output> (a dir).
    source_site: ?[]const u8 = null,
    source_count: ?u32 = null, // items per page/group; absent = default 5, 0 = all (applied in scrape.effectiveCount)
    group: u32 = 0, // 0-based page index; window = filtered[G*N : G*N+N]
    source_filter: ?[]const u8 = null, // category tokens, '|'-separated (absent = all)
    source_format: ?[]const u8 = null, // format tokens, '|'-separated (absent = all)
    source_name: ?[]const u8 = null, // regex (POSIX ERE, case-insensitive) on the media URL; absent = all
    source_min_width: u32 = 0, // inclusive; 0 = unset
    source_max_width: u32 = 0,
    source_min_height: u32 = 0,
    source_max_height: u32 = 0,
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
        } else if (eq(arg, "--console") or eq(arg, "--repl")) {
            opts.console = true;
        } else if (eq(arg, "-i") or eq(arg, "--input")) {
            if (opts.blank != null or opts.source_site != null) return Error.DuplicateSource;
            opts.input = try value(&st, "--input");
        } else if (eq(arg, "-f") or eq(arg, "--frame")) {
            opts.frame = try parseU32(try value(&st, "--frame"));
        } else if (eq(arg, "--blank")) {
            if (opts.input != null or opts.source_site != null) return Error.DuplicateSource;
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
        } else if (eq(arg, "--server")) {
            if (opts.source_site != null) return Error.DuplicateSource;
            opts.server = try value(&st, "--server");
        } else if (eq(arg, "--source-site")) {
            if (opts.input != null or opts.blank != null or opts.server != null) return Error.DuplicateSource;
            opts.source_site = try value(&st, "--source-site");
        } else if (eq(arg, "--source-count")) {
            opts.source_count = try parseU32(try value(&st, "--source-count"));
        } else if (eq(arg, "--group")) {
            opts.group = try parseU32(try value(&st, "--group"));
        } else if (eq(arg, "--source-filter")) {
            opts.source_filter = try value(&st, "--source-filter");
        } else if (eq(arg, "--source-format")) {
            opts.source_format = try value(&st, "--source-format");
        } else if (eq(arg, "--source-name")) {
            opts.source_name = try value(&st, "--source-name");
        } else if (eq(arg, "--source-min-width")) {
            opts.source_min_width = try parseU32(try value(&st, "--source-min-width"));
        } else if (eq(arg, "--source-max-width")) {
            opts.source_max_width = try parseU32(try value(&st, "--source-max-width"));
        } else if (eq(arg, "--source-min-height")) {
            opts.source_min_height = try parseU32(try value(&st, "--source-min-height"));
        } else if (eq(arg, "--source-max-height")) {
            opts.source_max_height = try parseU32(try value(&st, "--source-max-height"));
        } else if (eq(arg, "--remote")) {
            opts.remote = try value(&st, "--remote");
        } else if (eq(arg, "--remote-name")) {
            opts.remote_name = try value(&st, "--remote-name");
        } else if (eq(arg, "--remote-update")) {
            opts.remote_update = true;
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

// --blank consumes an optional page-format name ("a4", "B5", …; case-insensitive, stored
// canonical), or an optional `width height` pair (both must be integers; omit both for the
// default page size), followed by an optional colour. Tokens are only consumed when they
// match, so `--blank out.png`, `--blank b5 out.png` and `--blank 800 600 red out` all
// parse without swallowing the output path.
fn parseBlank(allocator: std.mem.Allocator, st: *ParseState) Error!Blank {
    var b = Blank{};
    if (st.i < st.argv.len) {
        if (core.canonicalPageFormat(st.argv[st.i])) |name| {
            b.page = name;
            st.i += 1;
        }
    }
    if (peekU32(st)) |w| {
        // A format token names the size, so it excludes explicit dims.
        if (b.page != null) {
            std.debug.print("error: --blank takes a page format OR explicit dims, not both\n", .{});
            return Error.BadNumber;
        }
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
    try testing.expect(o2.blank.?.page == null);
    try testing.expect(o2.blank.?.width == null);
    try testing.expectEqualStrings("white", o2.blank.?.color);
    try testing.expectEqualStrings("out.png", o2.output.?);
}

test "parse: blank optional page-format token" {
    const a = testing.allocator;
    // A leading format name (any case) picks the page; the colour still parses after it.
    const a1 = [_][:0]const u8{ "--blank", "b5", "pink", "out.png" };
    const o1 = try parse(a, &a1);
    try testing.expectEqualStrings("B5", o1.blank.?.page.?);
    try testing.expect(o1.blank.?.width == null);
    try testing.expectEqualStrings("pink", o1.blank.?.color);
    try testing.expectEqualStrings("out.png", o1.output.?);

    const a2 = [_][:0]const u8{ "--blank", "A5", "out.png" };
    const o2 = try parse(a, &a2);
    try testing.expectEqualStrings("A5", o2.blank.?.page.?);
    try testing.expectEqualStrings("out.png", o2.output.?);

    // A format token and explicit dims are mutually exclusive.
    const a3 = [_][:0]const u8{ "--blank", "b5", "800", "600", "out.png" };
    try testing.expectError(Error.BadNumber, parse(a, &a3));
}

test "parse: --console / --repl activate console mode" {
    const a = testing.allocator;
    const c1 = [_][:0]const u8{"--console"};
    try testing.expect((try parse(a, &c1)).console);
    const c2 = [_][:0]const u8{"--repl"};
    try testing.expect((try parse(a, &c2)).console);
    const c3 = [_][:0]const u8{ "-i", "in.png", "out.png" };
    try testing.expect(!(try parse(a, &c3)).console);
}

test "parse: input and blank are mutually exclusive" {
    const a = testing.allocator;
    const argv = [_][:0]const u8{ "-i", "x.png", "--blank", "10", "10" };
    try testing.expectError(Error.DuplicateSource, parse(a, &argv));
}

test "parse: server options" {
    const a = testing.allocator;
    const argv = [_][:0]const u8{
        "--server",        "http://h:8090", "-i", "proj-name",
        "--remote-update", "out.png",
    };
    const o = try parse(a, &argv);
    try testing.expectEqualStrings("http://h:8090", o.server.?);
    try testing.expect(o.remote_update);
    try testing.expectEqualStrings("proj-name", o.input.?);

    const a2 = [_][:0]const u8{
        "-i", "in.png", "--remote", "http://h:8090", "--remote-name", "Shared", "out.png",
    };
    const o2 = try parse(a, &a2);
    try testing.expectEqualStrings("http://h:8090", o2.remote.?);
    try testing.expectEqualStrings("Shared", o2.remote_name.?);
}

test "parse: source-site scrape flags" {
    const a = testing.allocator;
    const argv = [_][:0]const u8{
        "--source-site",   "https://example.com/",
        "--source-count",  "3",
        "--group",         "1",
        "--source-filter", "img|background",
        "--source-format", "png|jpg",
        "--source-name",   "cat.*\\.jpg",
        "--source-min-width", "100",
        "--source-max-height", "800",
        "out",
    };
    const o = try parse(a, &argv);
    try testing.expectEqualStrings("https://example.com/", o.source_site.?);
    try testing.expectEqual(@as(u32, 3), o.source_count.?);
    try testing.expectEqual(@as(u32, 1), o.group);
    try testing.expectEqualStrings("img|background", o.source_filter.?);
    try testing.expectEqualStrings("png|jpg", o.source_format.?);
    try testing.expectEqualStrings("cat.*\\.jpg", o.source_name.?);
    try testing.expectEqual(@as(u32, 100), o.source_min_width);
    try testing.expectEqual(@as(u32, 800), o.source_max_height);
    try testing.expectEqualStrings("out", o.output.?);
    // Defaults when absent.
    const bare = [_][:0]const u8{ "--source-site", "https://x/", "dir" };
    const ob = try parse(a, &bare);
    try testing.expect(ob.source_count == null);
    try testing.expectEqual(@as(u32, 0), ob.group);
}

test "parse: source-site is mutually exclusive with -i / --blank / --server" {
    const a = testing.allocator;
    const a1 = [_][:0]const u8{ "--source-site", "https://x/", "-i", "in.png" };
    try testing.expectError(Error.DuplicateSource, parse(a, &a1));
    const a2 = [_][:0]const u8{ "-i", "in.png", "--source-site", "https://x/" };
    try testing.expectError(Error.DuplicateSource, parse(a, &a2));
    const a3 = [_][:0]const u8{ "--blank", "--source-site", "https://x/" };
    try testing.expectError(Error.DuplicateSource, parse(a, &a3));
    const a4 = [_][:0]const u8{ "--source-site", "https://x/", "--server", "http://h" };
    try testing.expectError(Error.DuplicateSource, parse(a, &a4));
}
