//! The flag parser: argv in, Options out. No I/O and no globals, so every grammar rule
//! (and every rejection) is unit-tested directly. The loop is here; a feature with a block
//! of flags of its own parses them in its own file (blank, scrape, inspect).
const std = @import("std");
const logo = @import("../app/logo.zig");
const testing = std.testing;
const options = @import("options.zig");
const state = @import("state.zig");
const blank = @import("blank.zig");
const scrape = @import("scrape.zig");
const inspect = @import("inspect.zig");
const plan = @import("plan.zig");
const thumbnail = @import("thumbnail.zig");
const Error = options.Error;
const LayoutFrame = options.LayoutFrame;
const Options = options.Options;
const ParseState = state.ParseState;
const value = state.value;
const parseU32 = state.parseU32;
const parseI32 = state.parseI32;
const eq = state.eq;

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
            opts.blank = try blank.parseBlank(&st);
        } else if (eq(arg, "-c") or eq(arg, "--crop")) {
            opts.crop = try value(&st, "--crop");
        } else if (eq(arg, "--album")) {
            opts.album = true;
        } else if (eq(arg, "--flip")) {
            opts.flip = true;
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
        } else if (eq(arg, "--no-clobber")) {
            opts.no_clobber = true;
        } else if (eq(arg, "--thumbnail")) {
            opts.thumbnail = try thumbnail.side(try value(&st, "--thumbnail"));
        } else if (eq(arg, "--server")) {
            if (opts.source_site != null) return Error.DuplicateSource;
            opts.server = try value(&st, "--server");
        } else if (eq(arg, "--remote")) {
            opts.remote = try value(&st, "--remote");
        } else if (eq(arg, "--remote-name")) {
            opts.remote_name = try value(&st, "--remote-name");
        } else if (eq(arg, "--remote-update")) {
            opts.remote_update = true;
        } else if (eq(arg, "--token")) {
            opts.token = try value(&st, "--token");
        } else if ((try scrape.flag(&opts, arg, &st)) or (try inspect.flag(&opts, arg, &st)) or (try plan.flag(&opts, arg, &st))) {} else if (arg.len > 1 and arg[0] == '-' and !looksNegativeNumber(arg)) {
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
    try plan.finish(opts);
    try inspect.finish(opts);
    try thumbnail.finish(opts);
    return opts;
}

// "-1", "-90" etc. are values, not flags (so they aren't misread as unknown flags when
// they appear as a stray positional — rotate values are consumed positionally above).
fn looksNegativeNumber(a: []const u8) bool {
    if (a.len < 2 or a[0] != '-') return false;
    for (a[1..]) |ch| if (!std.ascii.isDigit(ch)) return false;
    return true;
}

test "parse: flags and positional output" {
    const argv = [_][:0]const u8{ "-i", "in.png", "-r", "-1", "--flip", "--album", "-c", "x1=10%", "out.png" };
    const o = try parse(&argv);
    try testing.expectEqualStrings("in.png", o.input.?);
    try testing.expectEqual(@as(i32, -1), o.rotate);
    try testing.expect(o.flip);
    try testing.expect(o.album);
    try testing.expectEqualStrings("x1=10%", o.crop.?);
    try testing.expectEqualStrings("out.png", o.output.?);
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

test "parse: --no-clobber is a switch, off unless given" {
    const on = [_][:0]const u8{ "-i", "in.png", "--no-clobber", "out" };
    const o = try parse(&on);
    try testing.expect(o.no_clobber);
    try testing.expectEqualStrings("out", o.output.?);
    const off = [_][:0]const u8{ "-i", "in.png", "out" };
    try testing.expect(!(try parse(&off)).no_clobber);
}

test {
    _ = blank;
    _ = scrape;
    _ = inspect;
    _ = plan;
    _ = state;
    _ = thumbnail;
}
