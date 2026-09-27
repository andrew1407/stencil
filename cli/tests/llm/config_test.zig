//! LLM provider configuration (contract §5): the env defaults per provider, a provider change
//! re-filling the default URL, the anthropic session key's clock, and the `/llm` grammar.
const std = @import("std");
const config = @import("../../src/llm/config.zig");
const providers = @import("../../src/llm/providers.zig");
const Config = config.Config;
const parseCmd = config.parseCmd;
const testing = std.testing;

test "config: env defaults per provider (contract §5)" {
    const a = testing.allocator;

    // No env at all → ollama + its default baseUrl.
    var c1 = try Config.init(a, .{});
    defer c1.deinit(a);
    try testing.expect(c1.provider == .ollama);
    try testing.expectEqualStrings("http://localhost:11434", c1.base_url);
    try testing.expectEqualStrings("", c1.model);
    try testing.expectEqualStrings("", c1.api_key);
    try testing.expectEqualStrings("", c1.server_url);
    try testing.expectEqualStrings("", c1.server_token);

    // Provider from env picks that provider's default URL; the other keys carry over.
    var c2 = try Config.init(a, .{ .provider = "openai-compat", .model = "qwen", .api_key = "sk-1" });
    defer c2.deinit(a);
    try testing.expect(c2.provider == .openai_compat);
    try testing.expectEqualStrings("http://localhost:1234/v1", c2.base_url);
    try testing.expectEqualStrings("qwen", c2.model);
    try testing.expectEqualStrings("sk-1", c2.api_key);

    // An explicit env base URL wins over the default (trailing '/' trimmed) …
    var c3 = try Config.init(a, .{ .base_url = "http://box:9999/" });
    defer c3.deinit(a);
    try testing.expectEqualStrings("http://box:9999", c3.base_url);

    // … and stencil-server has no baseUrl default; serverUrl + token load from env.
    var c4 = try Config.init(a, .{ .provider = "stencil-server", .server_url = "https://s:8090/", .server_token = "tok" });
    defer c4.deinit(a);
    try testing.expect(c4.provider == .stencil_server);
    try testing.expectEqualStrings("", c4.base_url);
    try testing.expectEqualStrings("https://s:8090", c4.server_url);
    try testing.expectEqualStrings("tok", c4.server_token);

    // Unknown provider token / empty values fall back to the defaults.
    var c5 = try Config.init(a, .{ .provider = "frobnicator", .base_url = "  " });
    defer c5.deinit(a);
    try testing.expect(c5.provider == .ollama);
    try testing.expectEqualStrings("http://localhost:11434", c5.base_url);
}

test "config: provider change re-fills the default url unless overridden this session" {
    const a = testing.allocator;

    // Env-supplied URL is an initial value, not a session override → provider change refills.
    var c = try Config.init(a, .{ .base_url = "http://box:9999" });
    defer c.deinit(a);
    try c.setProvider(a, .openai_compat);
    try testing.expectEqualStrings("http://localhost:1234/v1", c.base_url);

    // A '/llm url' override survives provider changes.
    try c.setBaseUrl(a, "http://mine:1/v2/");
    try testing.expectEqualStrings("http://mine:1/v2", c.base_url);
    try c.setProvider(a, .ollama);
    try testing.expectEqualStrings("http://mine:1/v2", c.base_url);
}

test "parseCmd: /llm sub-command grammar" {
    try testing.expect(parseCmd("") == .show);
    try testing.expect(parseCmd("   ") == .show);
    try testing.expect(parseCmd("provider ollama").provider == .ollama);
    try testing.expect(parseCmd("provider OPENAI-COMPAT").provider == .openai_compat);
    try testing.expect(parseCmd("provider stencil-server").provider == .stencil_server);
    try testing.expectEqualStrings("gpt5", parseCmd("provider gpt5").bad_provider);
    try testing.expectEqualStrings("http://x:1", parseCmd("url http://x:1").url);
    try testing.expectEqualStrings("http://x:1", parseCmd("baseurl  http://x:1 ").url);
    try testing.expectEqualStrings("llava", parseCmd("model llava").model);
    try testing.expectEqualStrings("sk-2", parseCmd("key sk-2").key);
    try testing.expectEqualStrings("https://s:8090", parseCmd("server https://s:8090").server);
    try testing.expect(parseCmd("provider") == .usage); // missing value
    try testing.expect(parseCmd("url") == .usage);
    try testing.expect(parseCmd("frobnicate x") == .usage); // unknown sub-command
}

test "config: anthropic from the environment — its default url and the key it holds (§5)" {
    const a = testing.allocator;
    var c = try Config.init(a, .{ .provider = "anthropic", .api_key = " sk-ant-env-0123456789 " });
    defer c.deinit(a);
    try testing.expect(c.provider == .anthropic);
    try testing.expectEqualStrings("https://api.anthropic.com", c.base_url);
    try testing.expectEqualStrings("sk-ant-env-0123456789", c.api_key);
    try testing.expectEqual(@as(i64, 0), c.key_expires_ms); // armed by its first use, not here
    var b = try Config.init(a, .{ .provider = "anthropic", .base_url = "http://127.0.0.1:9/", .model = "claude-haiku-4-5" });
    defer b.deinit(a);
    try testing.expectEqualStrings("http://127.0.0.1:9", b.base_url);
    try testing.expectEqualStrings("claude-haiku-4-5", b.model);
    try testing.expectEqualStrings("", b.api_key);
}

test "config: the anthropic key's session clock — armed once, dropped at the TTL, zeroed" {
    const a = testing.allocator;
    const ttl = providers.keyTtlMs();
    var c = try Config.init(a, .{ .provider = "anthropic", .api_key = "sk-ant-ttl-0123456789" });
    defer c.deinit(a);
    c.armKey(1_000);
    c.armKey(5_000); // a later use never extends it
    try testing.expectEqual(1_000 + ttl, c.key_expires_ms);
    try testing.expect(!c.expireKey(a, 1_000 + ttl - 1));
    try testing.expect(c.expireKey(a, 1_000 + ttl));
    try testing.expectEqualStrings("", c.api_key);
    try testing.expect(!c.expireKey(a, 1_000 + 2 * ttl)); // nothing left to drop

    // An openai-compat key has no session clock.
    var o = try Config.init(a, .{ .provider = "openai-compat", .api_key = "sk-local" });
    defer o.deinit(a);
    o.armKey(1_000);
    try testing.expectEqual(@as(i64, 0), o.key_expires_ms);
    try testing.expect(!o.expireKey(a, std.math.maxInt(i64)));
}

test "config: a key never follows a switch to or from anthropic" {
    const a = testing.allocator;
    var c = try Config.init(a, .{ .provider = "openai-compat", .api_key = "sk-local" });
    defer c.deinit(a);
    try c.setProvider(a, .ollama); // neither side is anthropic: kept
    try testing.expectEqualStrings("sk-local", c.api_key);
    try c.setProvider(a, .anthropic);
    try testing.expectEqualStrings("", c.api_key);
    try c.setApiKey(a, "sk-ant-switch-0123456789");
    c.armKey(0);
    try c.setProvider(a, .anthropic); // the same provider again: kept
    try testing.expectEqualStrings("sk-ant-switch-0123456789", c.api_key);
    try c.setProvider(a, .openai_compat);
    try testing.expectEqualStrings("", c.api_key);
    try testing.expectEqual(@as(i64, 0), c.key_expires_ms);
}

test "parseCmd: anthropic, and the key's hidden prompt and forget forms" {
    try testing.expect(parseCmd("provider anthropic").provider == .anthropic);
    try testing.expect(parseCmd("key") == .key_prompt);
    try testing.expect(parseCmd("apikey  ") == .key_prompt);
    try testing.expect(parseCmd("key forget") == .key_forget);
    try testing.expect(parseCmd("KEY Forget") == .key_forget);
    try testing.expectEqualStrings("sk-ant-x", parseCmd("key sk-ant-x").key);
}
