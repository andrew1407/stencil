//! The `--source-site` block: the scrape mode's page, window and filter flags, and the rule
//! that a scrape takes no other source. An absent count is the default 5 and `0` every match,
//! the window being `filtered[group*count : group*count+count]`.
const std = @import("std");
const testing = std.testing;
const options = @import("options.zig");
const state = @import("state.zig");
const parser = @import("parse.zig");

const Error = options.Error;
const Options = options.Options;
const ParseState = state.ParseState;
const value = state.value;
const parseU32 = state.parseU32;
const eq = state.eq;

/// Consume `arg` when it is a scrape flag; false leaves it to the caller.
pub fn flag(opts: *Options, arg: []const u8, st: *ParseState) Error!bool {
    if (eq(arg, "--source-site")) {
        if (opts.input != null or opts.blank != null or opts.server != null) return Error.DuplicateSource;
        opts.source_site = try value(st, "--source-site");
    } else if (eq(arg, "--source-count")) {
        opts.source_count = try parseU32(try value(st, "--source-count"));
    } else if (eq(arg, "--group")) {
        opts.group = try parseU32(try value(st, "--group"));
    } else if (eq(arg, "--source-filter")) {
        opts.source_filter = try value(st, "--source-filter");
    } else if (eq(arg, "--source-format")) {
        opts.source_format = try value(st, "--source-format");
    } else if (eq(arg, "--source-name")) {
        opts.source_name = try value(st, "--source-name");
    } else if (eq(arg, "--source-min-width")) {
        opts.source_min_width = try parseU32(try value(st, "--source-min-width"));
    } else if (eq(arg, "--source-max-width")) {
        opts.source_max_width = try parseU32(try value(st, "--source-max-width"));
    } else if (eq(arg, "--source-min-height")) {
        opts.source_min_height = try parseU32(try value(st, "--source-min-height"));
    } else if (eq(arg, "--source-max-height")) {
        opts.source_max_height = try parseU32(try value(st, "--source-max-height"));
    } else return false;
    return true;
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
    const o = try parser.parse(&argv);
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
    const ob = try parser.parse(&bare);
    try testing.expect(ob.source_count == null);
    try testing.expectEqual(@as(u32, 0), ob.group);
}

test "parse: source-site is mutually exclusive with -i / --blank / --server" {
    const a1 = [_][:0]const u8{ "--source-site", "https://x/", "-i", "in.png" };
    try testing.expectError(Error.DuplicateSource, parser.parse(&a1));
    const a2 = [_][:0]const u8{ "-i", "in.png", "--source-site", "https://x/" };
    try testing.expectError(Error.DuplicateSource, parser.parse(&a2));
    const a3 = [_][:0]const u8{ "--blank", "--source-site", "https://x/" };
    try testing.expectError(Error.DuplicateSource, parser.parse(&a3));
    const a4 = [_][:0]const u8{ "--source-site", "https://x/", "--server", "http://h" };
    try testing.expectError(Error.DuplicateSource, parser.parse(&a4));
}
