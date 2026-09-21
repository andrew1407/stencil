//! The flag parser: argv in, Options out. No I/O and no globals, so every grammar rule
//! (and every rejection) is unit-tested directly.
const std = @import("std");
const core = @import("../core.zig");
const logo = @import("../app/logo.zig");
const testing = std.testing;
const options = @import("options.zig");
const Blank = options.Blank;
const Error = options.Error;
const LayoutFrame = options.LayoutFrame;
const Options = options.Options;

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
        logo.err("{s} expects a value\n", .{flag});
        return Error.MissingValue;
    };
}

fn parseU32(s: []const u8) Error!u32 {
    return std.fmt.parseInt(u32, s, 10) catch return Error.BadNumber;
}

fn parseI32(s: []const u8) Error!i32 {
    return std.fmt.parseInt(i32, s, 10) catch return Error.BadNumber;
}

/// Parse argv (excluding argv[0]). Allocation-free: argv strings already carry a NUL.
pub fn parse(argv: []const [:0]const u8) Error!Options {
    var opts = Options{};
    var st = ParseState{ .argv = argv, .i = 0 };

    while (st.next()) |arg| {
        if (eq(arg, "-h") or eq(arg, "--help")) {
            opts.help = true;
        } else if (eq(arg, "--console") or eq(arg, "--repl")) {
            opts.console = true;
        } else if (eq(arg, "--console-full-screen") or eq(arg, "--console-fullscreen")) {
            opts.console = true; // full-screen implies console mode
            opts.console_full_screen = true;
        } else if (eq(arg, "-i") or eq(arg, "--input")) {
            if (opts.blank != null or opts.source_site != null) return Error.DuplicateSource;
            opts.input = try value(&st, "--input");
        } else if (eq(arg, "-f") or eq(arg, "--frame")) {
            opts.frame = try parseU32(try value(&st, "--frame"));
        } else if (eq(arg, "--blank")) {
            if (opts.input != null or opts.source_site != null) return Error.DuplicateSource;
            opts.blank = try parseBlank(&st);
        } else if (eq(arg, "-c") or eq(arg, "--crop")) {
            opts.crop = try value(&st, "--crop");
        } else if (eq(arg, "--album")) {
            opts.album = true;
        } else if (eq(arg, "-r") or eq(arg, "--rotate")) {
            opts.rotate = try parseI32(try value(&st, "--rotate"));
        } else if (eq(arg, "-l") or eq(arg, "--layout")) {
            opts.layout = try value(&st, "--layout");
        } else if (eq(arg, "--layout-frame")) {
            const v = try value(&st, "--layout-frame");
            if (eq(v, "source")) {
                opts.layout_frame = .source;
            } else if (eq(v, "current")) {
                opts.layout_frame = .current;
            } else {
                logo.err("--layout-frame expects 'source' or 'current', got '{s}'\n", .{v});
                return Error.BadValue;
            }
        } else if (eq(arg, "--filter")) {
            opts.filter = try value(&st, "--filter");
        } else if (eq(arg, "--script")) {
            if (opts.script_plan != null or opts.script_check != null) return Error.DuplicateSource;
            opts.script = try value(&st, "--script");
        } else if (eq(arg, "--script-plan")) {
            if (opts.script != null or opts.script_check != null or opts.script_emit != null) return Error.DuplicateSource;
            opts.script_plan = try value(&st, "--script-plan");
        } else if (eq(arg, "--script-check")) {
            if (opts.script != null or opts.script_plan != null or opts.script_emit != null) return Error.DuplicateSource;
            opts.script_check = try value(&st, "--script-check");
        } else if (eq(arg, "--script-emit")) {
            if (opts.script_plan != null or opts.script_check != null) return Error.DuplicateSource;
            opts.script_emit = try value(&st, "--script-emit");
        } else if (eq(arg, "--confine-output")) {
            opts.confine_output = true;
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
        } else if (eq(arg, "--token")) {
            opts.token = try value(&st, "--token");
        } else if (arg.len > 1 and arg[0] == '-' and !looksNegativeNumber(arg)) {
            logo.err("unknown flag '{s}'\n", .{arg});
            return Error.UnknownFlag;
        } else {
            // A positional argument is the output path (last one wins).
            opts.output = arg;
        }
    }
    if (opts.script_emit != null and opts.script == null) {
        logo.err("--script-emit needs --script <file> to read\n", .{});
        return Error.BadValue;
    }
    return opts;
}

// --blank takes an optional page-format name (case-insensitive, stored canonical) or a `width height`
// pair, then an optional colour. Tokens are consumed only when they match, never the output path.
fn parseBlank(st: *ParseState) Error!Blank {
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
            logo.err("--blank takes a page format OR explicit dims, not both\n", .{});
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
        if (!(peek.len > 0 and peek[0] == '-') and core.parseColor(peek) != null) {
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

test "parse: flags and positional output" {
    const argv = [_][:0]const u8{ "-i", "in.png", "-r", "-1", "--album", "-c", "x1=10%", "out.png" };
    const o = try parse(&argv);
    try testing.expectEqualStrings("in.png", o.input.?);
    try testing.expectEqual(@as(i32, -1), o.rotate);
    try testing.expect(o.album);
    try testing.expectEqualStrings("x1=10%", o.crop.?);
    try testing.expectEqualStrings("out.png", o.output.?);
}

test "parse: blank optional dims and colour" {
    const a1 = [_][:0]const u8{ "--blank", "800", "600", "red", "out.png" };
    const o1 = try parse(&a1);
    try testing.expectEqual(@as(u32, 800), o1.blank.?.width.?);
    try testing.expectEqualStrings("red", o1.blank.?.color);
    try testing.expectEqualStrings("out.png", o1.output.?);

    const a2 = [_][:0]const u8{ "--blank", "out.png" };
    const o2 = try parse(&a2);
    try testing.expect(o2.blank.?.page == null);
    try testing.expect(o2.blank.?.width == null);
    try testing.expectEqualStrings("white", o2.blank.?.color);
    try testing.expectEqualStrings("out.png", o2.output.?);
}

test "parse: blank optional page-format token" {
    // A leading format name (any case) picks the page; the colour still parses after it.
    const a1 = [_][:0]const u8{ "--blank", "b5", "pink", "out.png" };
    const o1 = try parse(&a1);
    try testing.expectEqualStrings("B5", o1.blank.?.page.?);
    try testing.expect(o1.blank.?.width == null);
    try testing.expectEqualStrings("pink", o1.blank.?.color);
    try testing.expectEqualStrings("out.png", o1.output.?);

    const a2 = [_][:0]const u8{ "--blank", "A5", "out.png" };
    const o2 = try parse(&a2);
    try testing.expectEqualStrings("A5", o2.blank.?.page.?);
    try testing.expectEqualStrings("out.png", o2.output.?);

    // A format token and explicit dims are mutually exclusive.
    const a3 = [_][:0]const u8{ "--blank", "b5", "800", "600", "out.png" };
    try testing.expectError(Error.BadNumber, parse(&a3));
}

test "parse: --layout-frame source/current; junk rejected" {
    const s = [_][:0]const u8{ "-i", "in.png", "-l", "lay.json", "--layout-frame", "source", "out.png" };
    try testing.expectEqual(LayoutFrame.source, (try parse(&s)).layout_frame);
    const c = [_][:0]const u8{ "-i", "in.png", "--layout-frame", "current", "out.png" };
    try testing.expectEqual(LayoutFrame.current, (try parse(&c)).layout_frame);
    // Default stays `current` (plain CLI behavior unchanged).
    const d = [_][:0]const u8{ "-i", "in.png", "out.png" };
    try testing.expectEqual(LayoutFrame.current, (try parse(&d)).layout_frame);
    const bad = [_][:0]const u8{ "-i", "in.png", "--layout-frame", "snapshot" };
    try testing.expectError(Error.BadValue, parse(&bad));
}

test "parse: --console / --repl activate console mode" {
    const c1 = [_][:0]const u8{"--console"};
    try testing.expect((try parse(&c1)).console);
    const c2 = [_][:0]const u8{"--repl"};
    try testing.expect((try parse(&c2)).console);
    const c3 = [_][:0]const u8{ "-i", "in.png", "out.png" };
    try testing.expect(!(try parse(&c3)).console);
    // --console-full-screen implies console mode and sets the full-screen bit.
    const c4 = [_][:0]const u8{"--console-full-screen"};
    const o4 = try parse(&c4);
    try testing.expect(o4.console and o4.console_full_screen);
    try testing.expect(!(try parse(&c1)).console_full_screen); // plain --console stays line-oriented
}

test "parse: --script-emit rides with --script, and with neither reporting mode" {
    const ok = [_][:0]const u8{ "--script", "s.stc", "--script-emit", "s.pystc" };
    const o = try parse(&ok);
    try testing.expectEqualStrings("s.stc", o.script.?);
    try testing.expectEqualStrings("s.pystc", o.script_emit.?);

    // Nothing to read from, so the flag would otherwise be silently ignored.
    const alone = [_][:0]const u8{ "--script-emit", "s.pystc" };
    try testing.expectError(Error.BadValue, parse(&alone));

    const with_check = [_][:0]const u8{ "--script-emit", "s.py", "--script-check", "s.stc" };
    try testing.expectError(Error.DuplicateSource, parse(&with_check));
    const with_plan = [_][:0]const u8{ "--script-plan", "s.stc", "--script-emit", "s.py" };
    try testing.expectError(Error.DuplicateSource, parse(&with_plan));
}

test "parse: input and blank are mutually exclusive" {
    const argv = [_][:0]const u8{ "-i", "x.png", "--blank", "10", "10" };
    try testing.expectError(Error.DuplicateSource, parse(&argv));
}

test "parse: server options" {
    const argv = [_][:0]const u8{
        "--server",        "http://h:8090", "-i", "proj-name",
        "--remote-update", "out.png",
    };
    const o = try parse(&argv);
    try testing.expectEqualStrings("http://h:8090", o.server.?);
    try testing.expect(o.remote_update);
    try testing.expectEqualStrings("proj-name", o.input.?);

    const a2 = [_][:0]const u8{
        "-i", "in.png", "--remote", "http://h:8090", "--remote-name", "Shared", "out.png",
    };
    const o2 = try parse(&a2);
    try testing.expectEqualStrings("http://h:8090", o2.remote.?);
    try testing.expectEqualStrings("Shared", o2.remote_name.?);
    try testing.expect(o2.token == null); // no --token → self-issued session
}

test "parse: --token rides with --server / --remote" {
    const a1 = [_][:0]const u8{ "--server", "http://h:8090", "--token", "tok123", "-i", "proj", "out.png" };
    const o1 = try parse(&a1);
    try testing.expectEqualStrings("tok123", o1.token.?);
    try testing.expectEqualStrings("http://h:8090", o1.server.?);

    const a2 = [_][:0]const u8{ "-i", "in.png", "--remote", "http://h:8090", "--token", "s3cret", "out.png" };
    const o2 = try parse(&a2);
    try testing.expectEqualStrings("s3cret", o2.token.?);

    const missing = [_][:0]const u8{ "--server", "http://h:8090", "--token" };
    try testing.expectError(Error.MissingValue, parse(&missing));
}

test "parse: source-site scrape flags" {
    const argv = [_][:0]const u8{
        "--source-site",       "https://example.com/",
        "--source-count",      "3",
        "--group",             "1",
        "--source-filter",     "img|background",
        "--source-format",     "png|jpg",
        "--source-name",       "cat.*\\.jpg",
        "--source-min-width",  "100",
        "--source-max-height", "800",
        "out",
    };
    const o = try parse(&argv);
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
    const ob = try parse(&bare);
    try testing.expect(ob.source_count == null);
    try testing.expectEqual(@as(u32, 0), ob.group);
}

test "parse: source-site is mutually exclusive with -i / --blank / --server" {
    const a1 = [_][:0]const u8{ "--source-site", "https://x/", "-i", "in.png" };
    try testing.expectError(Error.DuplicateSource, parse(&a1));
    const a2 = [_][:0]const u8{ "-i", "in.png", "--source-site", "https://x/" };
    try testing.expectError(Error.DuplicateSource, parse(&a2));
    const a3 = [_][:0]const u8{ "--blank", "--source-site", "https://x/" };
    try testing.expectError(Error.DuplicateSource, parse(&a3));
    const a4 = [_][:0]const u8{ "--source-site", "https://x/", "--server", "http://h" };
    try testing.expectError(Error.DuplicateSource, parse(&a4));
}
