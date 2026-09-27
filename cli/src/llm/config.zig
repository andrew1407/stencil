//! Provider configuration for the console LLM assistant (contract §5): the
//! provider enum + default base URLs (providers.json), the STENCIL_LLM_* env
//! resolution, the session Config with its anthropic session key, and the /llm grammar.
const std = @import("std");
const providers = @import("providers.zig");

// Providers & configuration (contract §5)

pub const Provider = enum {
    ollama,
    openai_compat,
    anthropic,
    stencil_server,

    /// Parse a provider token (trimmed, case-insensitive); null when unknown.
    pub fn parse(token: []const u8) ?Provider {
        const t = std.mem.trim(u8, token, " \t");
        const eq = std.ascii.eqlIgnoreCase;
        if (eq(t, "ollama")) return .ollama;
        if (eq(t, "openai-compat")) return .openai_compat;
        if (eq(t, "anthropic")) return .anthropic;
        if (eq(t, "stencil-server")) return .stencil_server;
        return null;
    }

    pub fn label(self: Provider) []const u8 {
        return switch (self) {
            .ollama => "ollama",
            .openai_compat => "openai-compat",
            .anthropic => "anthropic",
            .stencil_server => "stencil-server",
        };
    }

    /// The contract's per-provider default `baseUrl` (§5), from the shared providers.json asset.
    /// `stencil-server` has none — its endpoint is the separately configured server URL.
    pub fn defaultBaseUrl(self: Provider) []const u8 {
        const d = providers.get();
        return switch (self) {
            .ollama => d.ollama_url,
            .openai_compat => d.openai_url,
            .anthropic => d.anthropic_url,
            .stencil_server => "",
        };
    }
};

/// The raw `STENCIL_LLM_*` values, borrowed from the process environ (stable for its lifetime). A
/// plain struct, so resolution into a `Config` is pure and testable without the real environment.
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
    api_key: []u8 = &.{}, // openai-compat's optional bearer, or the anthropic session key; "" = none
    key_expires_ms: i64 = 0, // wall-clock ms the anthropic key is dropped at; 0 = not yet armed
    server_url: []u8 = &.{}, // stencil-server only
    server_token: []u8 = &.{}, // stencil-server fallback token (no live /connect match)

    pub fn deinit(self: *Config, gpa: std.mem.Allocator) void {
        freeSlice(gpa, self.base_url);
        freeSlice(gpa, self.model);
        self.forgetKey(gpa);
        freeSlice(gpa, self.server_url);
        freeSlice(gpa, self.server_token);
        self.* = .{};
    }

    /// Resolve the environment into a config: provider defaults to "ollama", baseUrl per provider, empty
    /// values count as unset. Env URLs are initial values, NOT session overrides (see `/llm url`).
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
    /// URL this session (via `/llm url`). A key never follows a switch to or from anthropic.
    pub fn setProvider(self: *Config, gpa: std.mem.Allocator, p: Provider) !void {
        if ((self.provider == .anthropic) != (p == .anthropic)) self.forgetKey(gpa);
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

    /// Replace the key; an anthropic one starts its session clock at the caller's `armKey`.
    pub fn setApiKey(self: *Config, gpa: std.mem.Allocator, key: []const u8) !void {
        const dup = try gpa.dupe(u8, std.mem.trim(u8, key, " \t"));
        self.forgetKey(gpa);
        self.api_key = dup;
    }

    /// Start an anthropic key's session clock (§5): it is held until `now_ms` + ttlMinutes.
    pub fn armKey(self: *Config, now_ms: i64) void {
        if (self.provider != .anthropic or self.api_key.len == 0 or self.key_expires_ms != 0) return;
        self.key_expires_ms = now_ms + providers.keyTtlMs();
    }

    /// Drop an anthropic key whose time is up, before a request can carry it. True when it went.
    pub fn expireKey(self: *Config, gpa: std.mem.Allocator, now_ms: i64) bool {
        if (self.provider != .anthropic or self.api_key.len == 0 or self.key_expires_ms == 0) return false;
        if (now_ms < self.key_expires_ms) return false;
        self.forgetKey(gpa);
        return true;
    }

    /// Zero the key's bytes and free them.
    pub fn forgetKey(self: *Config, gpa: std.mem.Allocator) void {
        std.crypto.secureZero(u8, self.api_key);
        freeSlice(gpa, self.api_key);
        self.api_key = &.{};
        self.key_expires_ms = 0;
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
    key_prompt, // bare `/llm key`: ask for it with the input hidden
    key_forget, // `/llm key forget`
    server: []const u8,
};

pub fn parseCmd(arg: []const u8) Cmd {
    const t = std.mem.trim(u8, arg, " \t");
    if (t.len == 0) return .show;
    const sp = std.mem.indexOfAny(u8, t, " \t");
    const word = if (sp) |i| t[0..i] else t;
    const rest = if (sp) |i| std.mem.trim(u8, t[i + 1 ..], " \t") else "";
    const eq = std.ascii.eqlIgnoreCase;
    const key_word = eq(word, "key") or eq(word, "apikey");
    if (key_word and rest.len == 0) return .key_prompt;
    if (key_word and eq(rest, "forget")) return .key_forget;
    if (rest.len == 0) return .usage; // every other sub-command takes a value
    if (eq(word, "provider")) {
        return if (Provider.parse(rest)) |p| .{ .provider = p } else .{ .bad_provider = rest };
    }
    if (eq(word, "url") or eq(word, "base") or eq(word, "baseurl")) return .{ .url = rest };
    if (eq(word, "model")) return .{ .model = rest };
    if (key_word) return .{ .key = rest };
    if (eq(word, "server") or eq(word, "serverurl")) return .{ .server = rest };
    return .usage;
}
