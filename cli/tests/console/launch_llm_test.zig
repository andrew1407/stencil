//! `/open` with a launch link's assistant keys (launch-uri.md §2): the settings land through
//! `/llm`, but a link that moves the base URL drops the API key unless it brings its own, and
//! one that moves the server drops the stencil-server token, each said in a note.
const std = @import("std");
const console = @import("../../src/console.zig");
const llm = @import("../../src/llm.zig");
const logo = @import("../../src/app/logo.zig");
const Capture = @import("console_harness.zig").Capture;
const testing = std.testing;

const env: llm.Env = .{
    .provider = "openai-compat",
    .base_url = "https://llm.example/v1",
    .api_key = "sk-mine-0123456789",
    .server_url = "https://srv.example",
    .server_token = "tok-mine-0123456789",
};

fn run(link: []const u8, check: fn (*console.Session, []const u8) anyerror!void) !void {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = console.Session{ .gpa = a, .llm_env = env };
    defer session.deinit();
    _ = try console.handle(&session, threaded.io(), link);
    try check(&session, cap.text());
}

test "a link that moves the base URL drops the key, and says so without printing it" {
    try run("/open https://app.example/#llm-url=https://evil.example/v1", struct {
        fn check(s: *console.Session, out: []const u8) !void {
            try testing.expectEqualStrings("https://evil.example/v1", s.llm_cfg.?.base_url);
            try testing.expectEqualStrings("", s.llm_cfg.?.api_key);
            try testing.expectEqualStrings("tok-mine-0123456789", s.llm_cfg.?.server_token);
            try testing.expect(std.mem.indexOf(u8, out, "the API key held for the old endpoint was dropped") != null);
            try testing.expect(std.mem.indexOf(u8, out, "sk-mine") == null);
        }
    }.check);
}

test "a link that brings its own key keeps it; a path on the same origin keeps the stored one" {
    try run("/open https://app.example/#llm-url=https://evil.example/v1&llm-api-key=sk-theirs", struct {
        fn check(s: *console.Session, _: []const u8) !void {
            try testing.expectEqualStrings("sk-theirs", s.llm_cfg.?.api_key);
        }
    }.check);
    try run("/open https://app.example/#llm-url=https://llm.example/v2", struct {
        fn check(s: *console.Session, out: []const u8) !void {
            try testing.expectEqualStrings("sk-mine-0123456789", s.llm_cfg.?.api_key);
            try testing.expect(std.mem.indexOf(u8, out, "was dropped") == null);
        }
    }.check);
}

test "a link that moves the stencil-server URL drops the fallback token" {
    try run("/open https://app.example/#llm-server-url=https://evil.example", struct {
        fn check(s: *console.Session, out: []const u8) !void {
            try testing.expectEqualStrings("https://evil.example", s.llm_cfg.?.server_url);
            try testing.expectEqualStrings("", s.llm_cfg.?.server_token);
            try testing.expectEqualStrings("sk-mine-0123456789", s.llm_cfg.?.api_key);
            try testing.expect(std.mem.indexOf(u8, out, "the server token held for the old endpoint was dropped") != null);
        }
    }.check);
}
