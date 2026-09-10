//! Child processes (ffmpeg, the clipboard helpers) spawned WITHOUT the LLM environment:
//! `STENCIL_LLM_API_KEY` and its siblings are the user's credentials, and nothing the CLI
//! execs has any business inheriting them.
const std = @import("std");

const llm_prefix = "STENCIL_LLM_";

/// The parent environment, captured once by main(). Null in tests and other embedders, and
/// then a child simply inherits the process environment as `std.process.run` would.
var parent_env: ?*const std.process.Environ.Map = null;

pub fn captureEnv(map: *const std.process.Environ.Map) void {
    parent_env = map;
}

/// `std.process.run`, with the STENCIL_LLM_* keys taken out of the child's environment.
pub fn run(
    gpa: std.mem.Allocator,
    io: std.Io,
    options: std.process.RunOptions,
) (std.process.RunError || std.mem.Allocator.Error)!std.process.RunResult {
    var env = try scrubbedEnv(gpa);
    defer if (env) |*m| m.deinit();
    var opts = options;
    if (env) |*m| opts.environ_map = m;
    return std.process.run(gpa, io, opts);
}

/// A copy of the captured environment without the LLM keys; null when none was captured.
fn scrubbedEnv(gpa: std.mem.Allocator) !?std.process.Environ.Map {
    const src = parent_env orelse return null;
    var map: std.process.Environ.Map = .init(gpa);
    errdefer map.deinit();
    for (src.keys(), src.values()) |k, v| {
        if (std.ascii.startsWithIgnoreCase(k, llm_prefix)) continue;
        try map.put(k, v);
    }
    return map;
}

const testing = std.testing;

test "scrubbedEnv drops every STENCIL_LLM_* key and keeps the rest" {
    const a = testing.allocator;
    var env: std.process.Environ.Map = .init(a);
    defer env.deinit();
    try env.put("PATH", "/usr/bin");
    try env.put("STENCIL_LLM_API_KEY", "sk-secret");
    try env.put("STENCIL_LLM_SERVER_TOKEN", "tok");
    try env.put("stencil_llm_base_url", "http://localhost:1234/v1"); // Windows keys fold case

    captureEnv(&env);
    defer parent_env = null;
    var scrubbed = (try scrubbedEnv(a)).?;
    defer scrubbed.deinit();

    try testing.expectEqualStrings("/usr/bin", scrubbed.get("PATH").?);
    try testing.expect(scrubbed.get("STENCIL_LLM_API_KEY") == null);
    try testing.expect(scrubbed.get("STENCIL_LLM_SERVER_TOKEN") == null);
    try testing.expect(scrubbed.get("stencil_llm_base_url") == null);
    try testing.expectEqual(@as(usize, 1), scrubbed.keys().len);
}

test "no captured environment means no replacement map (plain inheritance)" {
    parent_env = null;
    try testing.expect((try scrubbedEnv(testing.allocator)) == null);
}
