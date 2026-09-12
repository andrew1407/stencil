//! Provider configuration for the console LLM assistant (contract §5): the
//! provider enum + default base URLs (providers.json), the STENCIL_LLM_* env
//! resolution, the session Config, and the /llm sub-command grammar.
const std = @import("std");


// Providers & configuration (contract §5)

pub const Provider = enum {
    ollama,
    openai_compat,
    stencil_server,

    /// Parse a provider token (trimmed, case-insensitive); null when unknown.
    pub fn parse(token: []const u8) ?Provider {
        const t = std.mem.trim(u8, token, " \t");
        const eq = std.ascii.eqlIgnoreCase;
        if (eq(t, "ollama")) return .ollama;
        if (eq(t, "openai-compat")) return .openai_compat;
        if (eq(t, "stencil-server")) return .stencil_server;
        return null;
    }

    pub fn label(self: Provider) []const u8 {
        return switch (self) {
            .ollama => "ollama",
            .openai_compat => "openai-compat",
            .stencil_server => "stencil-server",
        };
    }

    /// The contract's per-provider default `baseUrl` (§5), from the shared providers.json
    /// asset. `stencil-server` has none — its endpoint is the separately configured
    /// server URL (the asset says `null`).
    pub fn defaultBaseUrl(self: Provider) []const u8 {
        if (default_urls.ollama.len == 0) parseProviderDefaults();
        return switch (self) {
            .ollama => default_urls.ollama,
            .openai_compat => default_urls.openai_compat,
            .stencil_server => "",
        };
    }
};

// Canonical provider defaults (browser/js/config/llm/providers.json, embedded at build
// time). Parsed lazily on first use, like theme.zig: std.json needs an allocator.
// The strings slice into the embedded JSON (static lifetime); the CLI is single-threaded.
const providers_asset_json = @embedFile("providers.json");

var default_urls = struct {
    ollama: []const u8 = &.{},
    openai_compat: []const u8 = &.{},
}{};
var providers_scratch: [4096]u8 = undefined;

fn parseProviderDefaults() void {
    const Doc = struct {
        providers: struct {
            ollama: struct { defaultBaseUrl: []const u8 },
            @"openai-compat": struct { defaultBaseUrl: []const u8 },
        },
    };
    var fba = std.heap.FixedBufferAllocator.init(&providers_scratch);
    const doc = std.json.parseFromSliceLeaky(Doc, fba.allocator(), providers_asset_json, .{
        .ignore_unknown_fields = true,
    }) catch @panic("embedded providers.json is malformed");
    if (doc.providers.ollama.defaultBaseUrl.len == 0 or doc.providers.@"openai-compat".defaultBaseUrl.len == 0)
        @panic("embedded providers.json: empty default baseUrl");
    default_urls.ollama = doc.providers.ollama.defaultBaseUrl;
    default_urls.openai_compat = doc.providers.@"openai-compat".defaultBaseUrl;
}

/// The raw `STENCIL_LLM_*` environment values (slices borrowed from the process environ,
/// stable for the process lifetime). Kept as a plain struct so the resolution into a
/// `Config` is pure and unit-testable without touching the real environment.
pub const Env = struct {
    provider: ?[]const u8 = null, // STENCIL_LLM_PROVIDER
    base_url: ?[]const u8 = null, // STENCIL_LLM_BASE_URL
    model: ?[]const u8 = null, // STENCIL_LLM_MODEL
    api_key: ?[]const u8 = null, // STENCIL_LLM_API_KEY
    server_url: ?[]const u8 = null, // STENCIL_LLM_SERVER_URL
    server_token: ?[]const u8 = null, // STENCIL_LLM_SERVER_TOKEN

    pub fn fromMap(map: *const std.process.Environ.Map) Env {
        return .{
            .provider = map.get("STENCIL_LLM_PROVIDER"),
            .base_url = map.get("STENCIL_LLM_BASE_URL"),
            .model = map.get("STENCIL_LLM_MODEL"),
            .api_key = map.get("STENCIL_LLM_API_KEY"),
            .server_url = map.get("STENCIL_LLM_SERVER_URL"),
            .server_token = map.get("STENCIL_LLM_SERVER_TOKEN"),
        };
    }
};

/// The session's provider configuration (contract §5 shape). Initial values come from the
/// environment; `/llm …` sets in-session overrides. All strings owned.
pub const Config = struct {
    provider: Provider = .ollama,
    base_url: []u8 = &.{}, // ollama / openai-compat endpoint; trailing '/' trimmed
    url_overridden: bool = false, // '/llm url' was used this session → provider changes keep it
    model: []u8 = &.{}, // "" = provider/server default
    api_key: []u8 = &.{}, // openai-compat only; "" = no Authorization header
    server_url: []u8 = &.{}, // stencil-server only
    server_token: []u8 = &.{}, // stencil-server fallback token (no live /connect match)

    pub fn deinit(self: *Config, gpa: std.mem.Allocator) void {
        freeSlice(gpa, self.base_url);
        freeSlice(gpa, self.model);
        freeSlice(gpa, self.api_key);
        freeSlice(gpa, self.server_url);
        freeSlice(gpa, self.server_token);
        self.* = .{};
    }

    /// Resolve the environment into a config: provider defaults to "ollama", baseUrl per
    /// provider; empty env values count as unset. Env URLs are initial values, NOT session
    /// overrides — a provider change re-fills its default unless `/llm url` was used.
    pub fn init(gpa: std.mem.Allocator, env: Env) !Config {
        var cfg = Config{};
        errdefer cfg.deinit(gpa);
        if (nonEmpty(env.provider)) |p| {
            if (Provider.parse(p)) |prov| cfg.provider = prov;
        }
        const base = if (nonEmpty(env.base_url)) |u| u else cfg.provider.defaultBaseUrl();
        cfg.base_url = try gpa.dupe(u8, trimUrl(base));
        if (nonEmpty(env.model)) |m| cfg.model = try gpa.dupe(u8, m);
        if (nonEmpty(env.api_key)) |k| cfg.api_key = try gpa.dupe(u8, k);
        if (nonEmpty(env.server_url)) |s| cfg.server_url = try gpa.dupe(u8, trimUrl(s));
        if (nonEmpty(env.server_token)) |t| cfg.server_token = try gpa.dupe(u8, t);
        return cfg;
    }

    /// Switch providers; re-fills the default baseUrl unless the user already overrode the
    /// URL this session (via `/llm url`).
    pub fn setProvider(self: *Config, gpa: std.mem.Allocator, p: Provider) !void {
        self.provider = p;
        if (!self.url_overridden) {
            const dup = try gpa.dupe(u8, p.defaultBaseUrl());
            freeSlice(gpa, self.base_url);
            self.base_url = dup;
        }
    }

    pub fn setBaseUrl(self: *Config, gpa: std.mem.Allocator, url: []const u8) !void {
        const dup = try gpa.dupe(u8, trimUrl(url));
        freeSlice(gpa, self.base_url);
        self.base_url = dup;
        self.url_overridden = true;
    }

    pub fn setModel(self: *Config, gpa: std.mem.Allocator, model: []const u8) !void {
        const dup = try gpa.dupe(u8, std.mem.trim(u8, model, " \t"));
        freeSlice(gpa, self.model);
        self.model = dup;
    }

    pub fn setApiKey(self: *Config, gpa: std.mem.Allocator, key: []const u8) !void {
        const dup = try gpa.dupe(u8, std.mem.trim(u8, key, " \t"));
        freeSlice(gpa, self.api_key);
        self.api_key = dup;
    }

    pub fn setServerUrl(self: *Config, gpa: std.mem.Allocator, url: []const u8) !void {
        const dup = try gpa.dupe(u8, trimUrl(url));
        freeSlice(gpa, self.server_url);
        self.server_url = dup;
    }
};

fn freeSlice(gpa: std.mem.Allocator, s: []u8) void {
    if (s.len != 0) gpa.free(s);
}

fn nonEmpty(v: ?[]const u8) ?[]const u8 {
    const s = std.mem.trim(u8, v orelse return null, " \t");
    return if (s.len == 0) null else s;
}

pub fn trimUrl(url: []const u8) []const u8 {
    return std.mem.trimEnd(u8, std.mem.trim(u8, url, " \t"), "/");
}

// /llm command grammar (pure, unit-tested; the handler owns the printing)

pub const Cmd = union(enum) {
    show, // bare /llm
    usage, // unknown sub-command or a missing value
    provider: Provider,
    bad_provider: []const u8, // the unparsable token (slices into the arg)
    url: []const u8,
    model: []const u8,
    key: []const u8,
    server: []const u8,
};

pub fn parseCmd(arg: []const u8) Cmd {
    const t = std.mem.trim(u8, arg, " \t");
    if (t.len == 0) return .show;
    const sp = std.mem.indexOfAny(u8, t, " \t");
    const word = if (sp) |i| t[0..i] else t;
    const rest = if (sp) |i| std.mem.trim(u8, t[i + 1 ..], " \t") else "";
    const eq = std.ascii.eqlIgnoreCase;
    if (rest.len == 0) return .usage; // every sub-command takes a value
    if (eq(word, "provider")) {
        return if (Provider.parse(rest)) |p| .{ .provider = p } else .{ .bad_provider = rest };
    }
    if (eq(word, "url") or eq(word, "base") or eq(word, "baseurl")) return .{ .url = rest };
    if (eq(word, "model")) return .{ .model = rest };
    if (eq(word, "key") or eq(word, "apikey")) return .{ .key = rest };
    if (eq(word, "server") or eq(word, "serverurl")) return .{ .server = rest };
    return .usage;
}

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
