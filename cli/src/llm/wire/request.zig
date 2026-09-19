//! The three §6 provider request mappings: one ready-to-send URL, bearer and JSON body.
//! The replayed history rides before the current turn and the §4 suffix after the
//! canonical system prompt; the body shapes themselves live in body.zig.
const std = @import("std");
const config = @import("../config.zig");
const registry = @import("../registry.zig");
const chatDoc = @import("chatDoc.zig");
const body_mod = @import("body.zig");

const Config = config.Config;
const trimUrl = config.trimUrl;
const systemPrompt = registry.systemPrompt;
const Turn = chatDoc.Turn;
const boundedHistory = chatDoc.boundedHistory;
const writeBody = body_mod.writeBody;

/// One ready-to-send request: URL, optional `Authorization` value, JSON body. All owned.
pub const Request = struct {
    url: []u8,
    auth: ?[]u8 = null, // full header value ("Bearer <…>")
    body: []u8,

    pub fn deinit(self: *Request, gpa: std.mem.Allocator) void {
        gpa.free(self.url);
        if (self.auth) |a| gpa.free(a);
        gpa.free(self.body);
        self.* = .{ .url = &.{}, .body = &.{} };
    }
};

/// Build the provider request for one user turn (§6); `images` are the attached base64 PNGs in order.
/// For `stencil-server` the endpoint + bearer come from `server_url`/`server_token`.
pub fn buildRequest(
    gpa: std.mem.Allocator,
    cfg: *const Config,
    prompt: []const u8,
    images: []const []const u8,
    server_url: []const u8,
    server_token: []const u8,
) error{OutOfMemory}!Request {
    return buildRequestWithHistory(gpa, cfg, prompt, images, server_url, server_token, &.{}, "");
}

/// buildRequest with a replayed conversation and an optional system-prompt suffix: `history`
/// (text-only, most recent 32, §7/§12) rides BEFORE the current turn, the suffix after a blank line.
pub fn buildRequestWithHistory(
    gpa: std.mem.Allocator,
    cfg: *const Config,
    prompt: []const u8,
    images: []const []const u8,
    server_url: []const u8,
    server_token: []const u8,
    history: []const Turn,
    system_suffix: []const u8,
) error{OutOfMemory}!Request {
    return buildRequestWithSystem(gpa, cfg, systemPrompt(), prompt, images, server_url, server_token, history, system_suffix);
}

/// buildRequestWithHistory over an explicit system-prompt base: the console passes
/// `consoleSystemPrompt()`; wire shape, history replay and suffix joining are identical.
pub fn buildRequestWithSystem(
    gpa: std.mem.Allocator,
    cfg: *const Config,
    system_base: []const u8,
    prompt: []const u8,
    images: []const []const u8,
    server_url: []const u8,
    server_token: []const u8,
    history: []const Turn,
    system_suffix: []const u8,
) error{OutOfMemory}!Request {
    const joined: ?[]u8 = if (system_suffix.len == 0)
        null
    else
        try std.mem.join(gpa, "\n\n", &.{ system_base, system_suffix });
    defer if (joined) |j| gpa.free(j);
    const system: []const u8 = joined orelse system_base;

    var aw: std.Io.Writer.Allocating = .init(gpa);
    defer aw.deinit();
    var js: std.json.Stringify = .{ .writer = &aw.writer };
    writeBody(&js, cfg, system, prompt, images, boundedHistory(history)) catch return error.OutOfMemory;
    const body = try aw.toOwnedSlice();
    errdefer gpa.free(body);

    const url = switch (cfg.provider) {
        .ollama => try std.fmt.allocPrint(gpa, "{s}/api/chat", .{cfg.base_url}),
        .openai_compat => try std.fmt.allocPrint(gpa, "{s}/chat/completions", .{cfg.base_url}),
        .stencil_server => try std.fmt.allocPrint(gpa, "{s}/llm/chat", .{trimUrl(server_url)}),
    };
    errdefer gpa.free(url);

    const secret: []const u8 = switch (cfg.provider) {
        .ollama => "",
        .openai_compat => cfg.api_key,
        .stencil_server => server_token,
    };
    const auth: ?[]u8 = if (secret.len != 0)
        try std.fmt.allocPrint(gpa, "Bearer {s}", .{secret})
    else
        null;

    return .{ .url = url, .auth = auth, .body = body };
}
