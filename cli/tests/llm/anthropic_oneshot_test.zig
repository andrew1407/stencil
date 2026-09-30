//! The one-shot `--prompt` with the direct anthropic wire (llm-contract §5, §6.5), end to end over
//! a loopback Messages mock: the key from STENCIL_LLM_API_KEY rides as `x-api-key` beside
//! `anthropic-version` and nothing else, the plan runs, the output lands — and with no key,
//! nothing is sent at all.
const std = @import("std");
const logo = @import("../../src/app/logo.zig");
const image = @import("../../src/media/image.zig");
const llm = @import("../../src/llm.zig");
const project_cli = @import("../../src/project/cli.zig");
const Capture = @import("../console/console_harness.zig").Capture;
const Mock = @import("messages_mock.zig").Mock;
const testing = std.testing;

const test_key = "sk-ant-test-oneshot-0123456789";
const sample = "../common/samples/sample.png"; // 16x12
const plan_reply = "{\"model\":\"claude-opus-5\",\"stop_reason\":\"end_turn\",\"content\":[" ++
    "{\"type\":\"text\",\"text\":\"{\\\"version\\\":1,\\\"reply\\\":\\\"turned it\\\",\\\"actions\\\":[{\\\"op\\\":\\\"rotate\\\",\\\"dir\\\":\\\"right\\\"}]}\"}]}";

test "--prompt over anthropic: the §6.5 headers reach the endpoint and the plan's edit is written" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();

    const mock = try Mock.start("200 OK", plan_reply);
    defer mock.deinit();
    const base = try mock.base(a);
    defer a.free(base);
    const out = "stencil_prompt_anthropic.png";
    defer std.Io.Dir.cwd().deleteFile(io, out) catch {};

    try project_cli.runWith(a, io, .{ .input = sample, .prompt = "turn it right", .output = out }, .{
        .provider = "anthropic",
        .base_url = base,
        .api_key = test_key,
    });
    mock.stop();

    try testing.expectEqual(@as(usize, 1), mock.hits);
    try testing.expect(std.mem.startsWith(u8, mock.head.items, "POST /v1/messages "));
    try testing.expectEqualStrings(test_key, mock.header("x-api-key").?);
    try testing.expectEqualStrings(llm.providerDefaults().anthropic_version, mock.header("anthropic-version").?);
    try testing.expect(mock.header("authorization") == null);
    try testing.expect(mock.header("anthropic-dangerous-direct-browser-access") == null);
    // The body is the Messages shape with the defaults filled in; the key never rides in it.
    const body = try std.json.parseFromSlice(std.json.Value, a, mock.body.items, .{});
    defer body.deinit();
    try testing.expectEqualStrings(llm.providerDefaults().default_model, body.value.object.get("model").?.string);
    try testing.expectEqual(@as(i64, llm.providerDefaults().max_tokens), body.value.object.get("max_tokens").?.integer);
    try testing.expect(std.mem.indexOf(u8, mock.body.items, test_key) == null);

    var img = try decode(a, io, out);
    defer img.deinit(a);
    try testing.expectEqual(@as(usize, 12), img.width); // rotated right: 16x12 → 12x16
    try testing.expectEqual(@as(usize, 16), img.height);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "turned it") != null);
    try testing.expect(std.mem.indexOf(u8, cap.text(), test_key) == null);
}

test "--prompt over anthropic with no key sends nothing and fails as the typed disabled error" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();

    const mock = try Mock.start("200 OK", plan_reply);
    defer mock.deinit();
    const base = try mock.base(a);
    defer a.free(base);
    const out = "stencil_prompt_nokey.png";
    const run = project_cli.runWith(a, io, .{ .input = sample, .prompt = "turn it right", .output = out }, .{
        .provider = "anthropic",
        .base_url = base,
    });
    mock.stop();
    try testing.expectError(error.PromptFailed, run);
    try testing.expectEqual(@as(usize, 0), mock.hits);
    try testing.expect(std.mem.indexOf(u8, cap.text(), llm.no_key_message) != null);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "STENCIL_LLM_API_KEY") != null);
    try testing.expectError(error.FileNotFound, std.Io.Dir.cwd().access(io, out, .{}));
}

test "--prompt over anthropic: a rejected key is said once, in the contract's words" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();

    const rejected = "{\"type\":\"error\",\"error\":{\"type\":\"authentication_error\",\"message\":\"invalid x-api-key " ++ test_key ++ "\"}}";
    const mock = try Mock.start("401 Unauthorized", rejected);
    defer mock.deinit();
    const base = try mock.base(a);
    defer a.free(base);
    const run = project_cli.runWith(a, io, .{ .input = sample, .prompt = "turn it right", .output = "stencil_prompt_401.png" }, .{
        .provider = "anthropic",
        .base_url = base,
        .api_key = test_key,
    });
    mock.stop();
    try testing.expectError(error.PromptFailed, run);
    try testing.expectEqual(@as(usize, 1), mock.hits);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "the LLM provider rejected the API key\n") != null);
    try testing.expect(std.mem.indexOf(u8, cap.text(), test_key) == null);
}

test "the key never travels over plain http to anything but loopback" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();

    var cfg = try llm.Config.init(a, .{ .provider = "anthropic", .base_url = "http://api.example.com", .api_key = test_key });
    defer cfg.deinit(a);
    var req = try llm.buildRequest(a, &cfg, "hi", &.{}, "", "");
    defer req.deinit(a);
    try testing.expectError(error.LlmDisabled, llm.postJsonVia(a, threaded.io(), &req, null, refuse));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "refusing to send the API key to 'api.example.com' over plain http — use https\n") != null);
}

test "plain http: [::1] carries the key, a LAN host is refused as disabled before any send" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();

    var local = try llm.Config.init(a, .{ .provider = "anthropic", .base_url = "http://[::1]:8787", .api_key = test_key });
    defer local.deinit(a);
    var to_local = try llm.buildRequest(a, &local, "hi", &.{}, "", "");
    defer to_local.deinit(a);
    a.free(try llm.postJsonVia(a, threaded.io(), &to_local, null, Reached.send));
    try testing.expectEqualStrings("http://[::1]:8787/v1/messages", Reached.url);
    try testing.expect(Reached.keyed);

    var lan = try llm.Config.init(a, .{ .provider = "anthropic", .base_url = "http://192.168.1.5", .api_key = test_key });
    defer lan.deinit(a);
    var to_lan = try llm.buildRequest(a, &lan, "hi", &.{}, "", "");
    defer to_lan.deinit(a);
    try testing.expectError(error.LlmDisabled, llm.postJsonVia(a, threaded.io(), &to_lan, null, refuse));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "refusing to send the API key to '192.168.1.5' over plain http — use https\n") != null);
}

/// An exchange that answers 200, noting where the request went and whether it carried the key.
const Reached = struct {
    var url_buf: [64]u8 = undefined;
    var url: []const u8 = "";
    var keyed = false;

    fn send(gpa: std.mem.Allocator, _: std.Io, u: []const u8, hs: []const std.http.Header, _: []const u8) llm.PostError!@import("../../src/net.zig").Response {
        url = url_buf[0..@min(u.len, url_buf.len)];
        @memcpy(url_buf[0..url.len], u[0..url.len]);
        for (hs) |h| keyed = keyed or (std.mem.eql(u8, h.name, "x-api-key") and std.mem.eql(u8, h.value, test_key));
        return .{ .status = 200, .body = gpa.dupe(u8, "{}") catch return error.OutOfMemory };
    }
};

fn refuse(_: std.mem.Allocator, _: std.Io, _: []const u8, _: []const std.http.Header, _: []const u8) llm.PostError!@import("../../src/net.zig").Response {
    return error.HttpFailed; // reaching the exchange at all is the failure this test guards
}

fn decode(a: std.mem.Allocator, io: std.Io, path: []const u8) !image.Rgba8 {
    const bytes = try std.Io.Dir.cwd().readFileAlloc(io, path, a, .limited(4 << 20));
    defer a.free(bytes);
    return image.decode(a, bytes);
}
