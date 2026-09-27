//! The embedded `browser/js/config/llm/providers.json` (llm-contract §5–§6): each provider's
//! default base URL, and the anthropic wire's chat path, API version, default model and token
//! cap, and how long its session key is held. Parsed once on first use; every string slices
//! into the embedded asset, so it is static.
const std = @import("std");

const asset = @embedFile("providers.json");

pub const Defaults = struct {
    ollama_url: []const u8,
    openai_url: []const u8,
    anthropic_url: []const u8,
    anthropic_chat_path: []const u8,
    anthropic_version: []const u8, // anthropicUpstream.version: the `anthropic-version` header
    default_model: []const u8, // serverDefaults.model: an empty model means this one
    max_tokens: u32, // serverDefaults.maxTokens: the body's `max_tokens`
    key_ttl_minutes: u32, // providers.anthropic.sessionKey.ttlMinutes
};

var cached: ?Defaults = null;
var scratch: [4096]u8 = undefined;

/// The parsed defaults. The first call parses: `buildRequest` makes it on the calling thread
/// before any worker reads a value.
pub fn get() *const Defaults {
    if (cached == null) cached = parse();
    return &cached.?;
}

/// The anthropic session key's lifetime in ms.
pub fn keyTtlMs() i64 {
    return @as(i64, get().key_ttl_minutes) * std.time.ms_per_min;
}

fn parse() Defaults {
    const Doc = struct {
        providers: struct {
            ollama: struct { defaultBaseUrl: []const u8 },
            @"openai-compat": struct { defaultBaseUrl: []const u8 },
            anthropic: struct {
                defaultBaseUrl: []const u8,
                chatPath: []const u8,
                sessionKey: struct { ttlMinutes: u32 },
            },
        },
        anthropicUpstream: struct { version: []const u8 },
        serverDefaults: struct { model: []const u8, maxTokens: u32 },
    };
    var fba = std.heap.FixedBufferAllocator.init(&scratch);
    const doc = std.json.parseFromSliceLeaky(Doc, fba.allocator(), asset, .{
        .ignore_unknown_fields = true,
    }) catch @panic("embedded providers.json is malformed");
    const p = doc.providers;
    const d = Defaults{
        .ollama_url = p.ollama.defaultBaseUrl,
        .openai_url = p.@"openai-compat".defaultBaseUrl,
        .anthropic_url = p.anthropic.defaultBaseUrl,
        .anthropic_chat_path = p.anthropic.chatPath,
        .anthropic_version = doc.anthropicUpstream.version,
        .default_model = doc.serverDefaults.model,
        .max_tokens = doc.serverDefaults.maxTokens,
        .key_ttl_minutes = p.anthropic.sessionKey.ttlMinutes,
    };
    inline for (.{ d.ollama_url, d.openai_url, d.anthropic_url, d.anthropic_chat_path, d.anthropic_version, d.default_model }) |s| {
        if (s.len == 0) @panic("embedded providers.json: an empty default");
    }
    if (d.max_tokens == 0 or d.key_ttl_minutes == 0) @panic("embedded providers.json: a zero default");
    return d;
}

const testing = std.testing;

test "providers.json: the anthropic entry, its upstream constants and the session-key TTL" {
    const d = get();
    try testing.expectEqualStrings("https://api.anthropic.com", d.anthropic_url);
    try testing.expectEqualStrings("/v1/messages", d.anthropic_chat_path);
    try testing.expectEqualStrings("2023-06-01", d.anthropic_version);
    try testing.expect(d.default_model.len != 0 and d.max_tokens > 0);
    try testing.expectEqual(@as(i64, 720 * 60 * 1000), keyTtlMs());
}
