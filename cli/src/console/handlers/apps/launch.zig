//! The pure half of the app hand-offs: the browser's `#stencil=` launch fragment and the
//! desktop's `stencil://` link (the grammar of browser/js/core/launch/deepLink.js), JavaScript's
//! `encodeURIComponent`, and the argv of the OS opener. Nothing here spawns or prints.
const std = @import("std");
const builtin = @import("builtin");
const image = @import("../../../media/image.zig");
const codec = @import("../../../project/codec.zig");

const constants_json = @embedFile("constants.json");

/// The browser app's URL when none is configured (constants.json NETWORK.appUrl).
pub const app_url = comptimeStr("\"appUrl\": \"");
/// The longest fragment the browser reads (LAUNCH.fragmentMaxChars).
pub const fragment_max: usize = comptimeInt("\"fragmentMaxChars\": ");
/// The longest URL an OS opener takes as one argument: under Linux's 128 KiB cap on it.
pub const open_max: usize = 96 * 1024;

fn comptimeStr(comptime needle: []const u8) []const u8 {
    @setEvalBranchQuota(constants_json.len * 4);
    const at = std.mem.indexOf(u8, constants_json, needle).? + needle.len;
    return constants_json[at..][0..std.mem.indexOfScalar(u8, constants_json[at..], '"').?];
}

fn comptimeInt(comptime needle: []const u8) usize {
    @setEvalBranchQuota(constants_json.len * 4);
    const at = std.mem.indexOf(u8, constants_json, needle).? + needle.len;
    const end = std.mem.indexOfAny(u8, constants_json[at..], " ,}").?;
    return std.fmt.parseInt(usize, constants_json[at..][0..end], 10) catch @compileError("constants.json: " ++ needle);
}

/// JavaScript's `encodeURIComponent`: `A-Z a-z 0-9 - _ . ! ~ * ' ( )` pass, every other byte is `%XX`.
pub fn encodeUriComponent(gpa: std.mem.Allocator, s: []const u8) ![]u8 {
    var out: std.ArrayList(u8) = .empty;
    errdefer out.deinit(gpa);
    for (s) |b| {
        if (std.ascii.isAlphanumeric(b) or std.mem.indexOfScalar(u8, "-_.!~*'()", b) != null) {
            try out.append(gpa, b);
        } else {
            try out.appendSlice(gpa, &.{ '%', hexDigit(b >> 4), hexDigit(b & 0x0f) });
        }
    }
    return out.toOwnedSlice(gpa);
}

fn hexDigit(nibble: u8) u8 {
    return if (nibble < 10) '0' + nibble else 'A' + nibble - 10;
}

pub const ServerRef = struct { url: []const u8, id: []const u8, version: i64 };

/// What a launch carries: a server reference (the receiver re-fetches), else the picture inline.
pub const Payload = struct {
    server: ?ServerRef = null,
    image: []const u8 = "",
    fmt: image.Format = .png,
    name: []const u8 = "",
    layout_json: []const u8 = "{}",
    incognito: bool = false,
};

/// The `#stencil=` payload as JSON: `{server:{url,id,version}}` or `{dataUrl,name,layout}`,
/// plus `incognito: true` when asked (browser/js/core/launch/payload.js).
pub fn payloadJson(gpa: std.mem.Allocator, p: Payload) ![]u8 {
    var out: std.ArrayList(u8) = .empty;
    errdefer out.deinit(gpa);
    try out.append(gpa, '{');
    if (p.server) |s| {
        try out.appendSlice(gpa, "\"server\":{\"url\":");
        try codec.appendJsonString(gpa, &out, s.url);
        try out.appendSlice(gpa, ",\"id\":");
        try codec.appendJsonString(gpa, &out, s.id);
        try out.print(gpa, ",\"version\":{d}}}", .{s.version});
    } else {
        try out.appendSlice(gpa, "\"dataUrl\":\"data:");
        try out.appendSlice(gpa, mimeOf(p.fmt));
        try out.appendSlice(gpa, ";base64,");
        const enc = std.base64.standard.Encoder;
        const b64 = try gpa.alloc(u8, enc.calcSize(p.image.len));
        defer gpa.free(b64);
        try out.appendSlice(gpa, enc.encode(b64, p.image));
        try out.appendSlice(gpa, "\",\"name\":");
        try codec.appendJsonString(gpa, &out, p.name);
        try out.appendSlice(gpa, ",\"layout\":");
        try out.appendSlice(gpa, p.layout_json);
    }
    if (p.incognito) try out.appendSlice(gpa, ",\"incognito\":true");
    try out.append(gpa, '}');
    return out.toOwnedSlice(gpa);
}

pub fn mimeOf(fmt: image.Format) []const u8 {
    return switch (fmt) {
        .png => "image/png",
        .jpeg => "image/jpeg",
        .bmp => "image/bmp",
        .tga => "image/x-tga",
    };
}

/// `<base>#stencil=<encodeURIComponent(json)>`, the base as given.
pub fn browserUrl(gpa: std.mem.Allocator, base: []const u8, json: []const u8) ![]u8 {
    const enc = try encodeUriComponent(gpa, json);
    defer gpa.free(enc);
    return gpa.print("{s}#stencil={s}", .{ base, enc });
}

/// Whether a launch URL fits both the browser's fragment cap and the opener's argument.
pub fn fits(url_len: usize) bool {
    return url_len <= @min(fragment_max, open_max);
}

/// `stencil://open?server=…&id=…&version=N[&incognito=1]`, the link the desktop opens.
pub fn schemeUrl(gpa: std.mem.Allocator, ref: ServerRef, incognito: bool) ![]u8 {
    const url = try encodeUriComponent(gpa, ref.url);
    defer gpa.free(url);
    const id = try encodeUriComponent(gpa, ref.id);
    defer gpa.free(id);
    return gpa.print("stencil://open?server={s}&id={s}&version={d}{s}", .{ url, id, ref.version, if (incognito) "&incognito=1" else "" });
}

/// The platform opener with `target` last; `start`'s empty argument is its window title.
pub fn openerArgv(target: []const u8, buf: *[5][]const u8) []const []const u8 {
    return switch (builtin.os.tag) {
        .macos => blk: {
            buf.* = .{ "open", target, "", "", "" };
            break :blk buf[0..2];
        },
        .windows => blk: {
            buf.* = .{ "cmd", "/c", "start", "", target };
            break :blk buf[0..5];
        },
        else => blk: {
            buf.* = .{ "xdg-open", target, "", "", "" };
            break :blk buf[0..2];
        },
    };
}

/// A desktop binary run directly: `<bin> [--incognito] <target>`.
pub fn desktopArgv(bin: []const u8, incognito: bool, target: []const u8, buf: *[3][]const u8) []const []const u8 {
    if (incognito) {
        buf.* = .{ bin, "--incognito", target };
        return buf[0..3];
    }
    buf.* = .{ bin, target, "" };
    return buf[0..2];
}

const testing = std.testing;

test "encodeUriComponent matches JavaScript" {
    const a = testing.allocator;
    const cases = .{
        .{ "aZ0-_.!~*'()", "aZ0-_.!~*'()" },
        .{ "a b/c?d=e&f", "a%20b%2Fc%3Fd%3De%26f" },
        .{ "{\"x\":1}", "%7B%22x%22%3A1%7D" },
        .{ "é", "%C3%A9" },
    };
    inline for (cases) |c| {
        const got = try encodeUriComponent(a, c[0]);
        defer a.free(got);
        try testing.expectEqualStrings(c[1], got);
    }
}

test "the launch constants are constants.json's" {
    try testing.expectEqualStrings("http://localhost:8080/", app_url);
    try testing.expectEqual(@as(usize, 1_800_000), fragment_max);
    try testing.expect(fits(open_max) and !fits(open_max + 1));
}

test "payloadJson: a server reference, else the picture inline with its layout" {
    const a = testing.allocator;
    const remote = try payloadJson(a, .{ .server = .{ .url = "http://h:8090", .id = "p_1", .version = 7 }, .incognito = true });
    defer a.free(remote);
    try testing.expectEqualStrings("{\"server\":{\"url\":\"http://h:8090\",\"id\":\"p_1\",\"version\":7},\"incognito\":true}", remote);
    const local = try payloadJson(a, .{ .image = "ab", .fmt = .jpeg, .name = "a \"b\".jpg", .layout_json = "{\"lines\":[]}" });
    defer a.free(local);
    try testing.expectEqualStrings("{\"dataUrl\":\"data:image/jpeg;base64,YWI=\",\"name\":\"a \\\"b\\\".jpg\",\"layout\":{\"lines\":[]}}", local);
}

test "browserUrl and schemeUrl spell the deep-link grammar" {
    const a = testing.allocator;
    const b = try browserUrl(a, "http://localhost:8080/", "{\"x\":1}");
    defer a.free(b);
    try testing.expectEqualStrings("http://localhost:8080/#stencil=%7B%22x%22%3A1%7D", b);
    const s = try schemeUrl(a, .{ .url = "http://h:8090", .id = "p_1", .version = 3 }, false);
    defer a.free(s);
    try testing.expectEqualStrings("stencil://open?server=http%3A%2F%2Fh%3A8090&id=p_1&version=3", s);
    const i = try schemeUrl(a, .{ .url = "http://h", .id = "p", .version = 0 }, true);
    defer a.free(i);
    try testing.expect(std.mem.endsWith(u8, i, "&incognito=1"));
}

test "openerArgv names the platform opener with the target last; desktopArgv the binary" {
    var ob: [5][]const u8 = undefined;
    const argv = openerArgv("x.stencil", &ob);
    try testing.expectEqualStrings("x.stencil", argv[argv.len - 1]);
    try testing.expectEqualStrings(switch (builtin.os.tag) {
        .macos => "open",
        .windows => "cmd",
        else => "xdg-open",
    }, argv[0]);
    var db: [3][]const u8 = undefined;
    try testing.expectEqual(@as(usize, 3), desktopArgv("/opt/stencil", true, "x.stencil", &db).len);
    try testing.expectEqualStrings("--incognito", desktopArgv("/opt/stencil", true, "x.stencil", &db)[1]);
    try testing.expectEqual(@as(usize, 2), desktopArgv("/opt/stencil", false, "x.stencil", &db).len);
}
