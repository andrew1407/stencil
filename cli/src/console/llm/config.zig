//! `/llm` — the console's provider configuration (contract §5) and the context suffix the
//! assistant sees: which servers this session connected to, and the active project.
const std = @import("std");
const server = @import("../../serverClient.zig");
const logo = @import("../../logo.zig");
const llm = @import("../../llm.zig");
const Session = @import("../session.zig").Session;

// The wire/parse half lives in llm.zig (the CLI's port of llm-contract.md); this is the
// executor half: validated op-plan actions run through the SAME session operations the
// console commands use, variants through the same pipeline blocks the view rebuild uses.

/// `/llm [provider|url|model|key|server <value>]` — show (bare) or override the session's
/// LLM config, seeded from `STENCIL_LLM_*`; secrets are masked. A provider change re-fills
/// its default baseUrl unless the URL was overridden this session via '/llm url'.
pub fn doLlm(session: *Session, arg: []const u8) !void {
    const cfg = try session.llmConfig();
    switch (llm.parseCmd(arg)) {
        .show => showLlm(session, cfg),
        .usage => logo.print("usage: /llm [provider <ollama|openai-compat|stencil-server> | url <baseUrl> | model <name> | key <apiKey> | server <serverUrl>]\n", .{}),
        .bad_provider => |t| logo.err("unknown LLM provider '{s}' — ollama, openai-compat, or stencil-server\n", .{t}),
        .provider => |p| {
            try cfg.setProvider(session.gpa, p);
            if (p == .stencil_server) {
                logo.print("llm provider set to {s}\n", .{p.label()});
            } else {
                logo.print("llm provider set to {s} (url {s})\n", .{ p.label(), cfg.base_url });
            }
        },
        .url => |u| {
            try cfg.setBaseUrl(session.gpa, u);
            logo.print("llm url set to {s}\n", .{cfg.base_url});
        },
        .model => |m| {
            try cfg.setModel(session.gpa, m);
            logo.print("llm model set to {s}\n", .{cfg.model});
        },
        .key => |k| {
            try cfg.setApiKey(session.gpa, k);
            logo.print("llm api key set (hidden)\n", .{});
        },
        .server => |s| {
            try cfg.setServerUrl(session.gpa, s);
            logo.print("llm server set to {s}\n", .{cfg.server_url});
        },
    }
}

/// The bare `/llm` listing: every setting on its own row, secrets masked, plus where the
/// stencil-server auth would come from.
fn showLlm(session: *Session, cfg: *const llm.Config) void {
    logo.print("llm: provider {s} — '/llm provider|url|model|key|server <value>' to change (env: STENCIL_LLM_*)\n", .{cfg.provider.label()});
    logo.print("  url:    {s}\n", .{if (cfg.base_url.len != 0) cfg.base_url else "(none)"});
    logo.print("  model:  {s}\n", .{if (cfg.model.len != 0) cfg.model else "(provider default)"});
    logo.print("  key:    {s}\n", .{if (cfg.api_key.len != 0) "(set, hidden)" else "(not set)"});
    if (cfg.server_url.len != 0) {
        logo.print("  server: {s}\n", .{cfg.server_url});
    } else if (session.servers.items.len != 0) {
        logo.print("  server: (unset — would use the connected {s})\n", .{session.servers.items[0].base});
    } else {
        logo.print("  server: (not set)\n", .{});
    }
    logo.print("  token:  {s}\n", .{if (cfg.server_token.len != 0) "(set, hidden)" else "(not set — reuses a matching /connect token)"});
}

/// The resolved stencil-server endpoint + bearer for one call. `url` is owned by the
/// caller; `token` borrows from the live connection or the config.
pub const ServerAuth = struct { url: []u8, token: []const u8 };

/// Contract §5 (cli row): when /connect-ed to the configured serverUrl (or, when empty,
/// the first connection) reuse the live session token; else fall back to
/// STENCIL_LLM_SERVER_TOKEN. Null (with a message) when there is no server to talk to.
pub fn resolveServerAuth(session: *Session, cfg: *const llm.Config) !?ServerAuth {
    const gpa = session.gpa;
    if (cfg.server_url.len == 0) {
        if (session.servers.items.len != 0) {
            const c = &session.servers.items[0];
            return .{ .url = try gpa.dupe(u8, c.base), .token = c.token };
        }
        logo.err("the stencil-server provider needs a server — '/connect <url>' first, or '/llm server <url>' / STENCIL_LLM_SERVER_URL\n", .{});
        return null;
    }
    const base = try server.normalizeBase(gpa, cfg.server_url);
    if (session.findServer(base)) |c| return .{ .url = base, .token = c.token };
    return .{ .url = base, .token = cfg.server_token };
}

/// Build the console-context system-prompt suffix over `arena` (freed by the caller): one
/// llm.ConsoleServer per live connection — base URL plus, when `with_projects`, best-effort
/// project names. Tokens have no field to ride in; only c.base ever leaves here.
pub fn consoleContext(session: *Session, arena: std.mem.Allocator, with_projects: bool) error{OutOfMemory}![]u8 {
    const servers = try arena.alloc(llm.ConsoleServer, session.servers.items.len);
    for (session.servers.items, 0..) |*c, i| {
        servers[i] = .{
            .url = c.base,
            .active = session.remote_url != null and std.mem.eql(u8, session.remote_url.?, c.base),
            .projects = if (with_projects) try projectNames(c, arena) else null,
        };
    }
    const active: []const u8 = if (session.hasRemote()) (session.label orelse "") else "";
    return llm.consoleContextAlloc(arena, servers, active);
}

/// One server's project names (arena-owned), or null when the listing fails — the
/// context then omits that server's line rather than claiming it holds nothing.
fn projectNames(c: *server.Client, arena: std.mem.Allocator) error{OutOfMemory}!?[]const []const u8 {
    const infos = c.listProjectInfos() catch return null;
    defer server.freeProjectList(c.gpa, infos);
    const names = try arena.alloc([]const u8, infos.len);
    for (infos, 0..) |p, i| names[i] = try arena.dupe(u8, p.name);
    return names;
}
