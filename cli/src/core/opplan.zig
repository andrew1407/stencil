//! Typed Zig bridge over core's op-plan ABI (../core/abi/opplanShared.inc): the embedded
//! opRegistry.json resolved for a surface into a schema handle, and each model reply walked into
//! core's one result document. Every string core hands back is copied before its handle dies.
const std = @import("std");

const c = @cImport({
    @cInclude("core/cliApi.h");
});

/// The canonical registry (common/config/llm/opRegistry.json), embedded whole.
pub const registry_json = @embedFile("opRegistry.json");

/// The surfaces whose validator is core's (the extension keeps its own §8 walk).
pub const surfaces = [_][]const u8{ "cli", "mcp", "bot", "pystencil", "desktop", "browser" };

pub fn isSurface(name: []const u8) bool {
    for (surfaces) |s| if (std.mem.eql(u8, s, name)) return true;
    return false;
}

pub const Status = enum { valid, chat_only, invalid };

/// One reply as core walked it: `json` is {status, reply, actions, variants, ask, warnings, error}.
pub const Walked = struct { status: Status, json: []u8 };

pub const Schema = struct {
    id: c_int,

    /// `capabilities` null = every capability wired; else a comma list gating each op's `requires`.
    pub fn open(surface: [:0]const u8, capabilities: ?[:0]const u8) Schema {
        const caps: [*c]const u8 = if (capabilities) |s| s.ptr else null;
        return .{ .id = c.stencil_cli_opplanSchemaCreate(registry_json.ptr, @intCast(registry_json.len), surface.ptr, caps) };
    }

    pub fn close(self: Schema) void {
        c.stencil_cli_opplanSchemaDestroy(self.id);
    }

    /// Why core refused the registry for this surface, or "" (valid until `close`).
    pub fn failure(self: Schema) []const u8 {
        const why = c.stencil_cli_opplanSchemaError(self.id) orelse return "unknown schema handle";
        return std.mem.span(why);
    }

    /// The resolved surface as JSON (entries, forbidden, limits, registryBytes, registryFnv1a64);
    /// valid until `close`.
    pub fn entries(self: Schema) []const u8 {
        const text = c.stencil_cli_opplanSchemaEntries(self.id) orelse return "";
        return std.mem.span(text);
    }

    /// Walk one reply; the result text is `a`-owned.
    pub fn walk(self: Schema, a: std.mem.Allocator, raw: []const u8) error{ OutOfMemory, SchemaRefused }!Walked {
        const bytes: []const u8 = if (raw.len == 0) "" else raw;
        const p = c.stencil_cli_opplanParse(self.id, bytes.ptr, @intCast(bytes.len));
        if (p == 0) return error.SchemaRefused;
        defer c.stencil_cli_opplanDestroy(p);
        const status: Status = switch (c.stencil_cli_opplanStatus(p)) {
            0 => .valid,
            1 => .chat_only,
            else => .invalid,
        };
        return .{ .status = status, .json = try a.dupe(u8, std.mem.span(c.stencil_cli_opplanJson(p))) };
    }
};

const testing = std.testing;

test "opplan bridge: every core surface loads, an unknown one is refused with a reason" {
    for (surfaces) |name| {
        var buf: [16]u8 = undefined;
        const z = try std.fmt.bufPrintZ(&buf, "{s}", .{name});
        const s = Schema.open(z, null);
        defer s.close();
        try testing.expectEqualStrings("", s.failure());
        try testing.expect(std.mem.indexOf(u8, s.entries(), "\"registryFnv1a64\"") != null);
    }
    const bad = Schema.open("toaster", null);
    defer bad.close();
    try testing.expect(std.mem.indexOf(u8, bad.failure(), "unknown surface") != null);
    try testing.expectError(error.SchemaRefused, bad.walk(testing.allocator, "{}"));
}

test "opplan bridge: the forbidden policy follows the surface" {
    const raw = "{\"reply\":\"x\",\"actions\":[{\"op\":\"llm\"}]}";
    const cli = Schema.open("cli", null);
    defer cli.close();
    const w1 = try cli.walk(testing.allocator, raw);
    defer testing.allocator.free(w1.json);
    try testing.expectEqual(Status.invalid, w1.status);
    const bot = Schema.open("bot", null);
    defer bot.close();
    const w2 = try bot.walk(testing.allocator, raw);
    defer testing.allocator.free(w2.json);
    try testing.expectEqual(Status.valid, w2.status);
    try testing.expect(std.mem.indexOf(u8, w2.json, "W_UNKNOWN_OP") != null);
}
