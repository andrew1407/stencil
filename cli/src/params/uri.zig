//! The launch URI (contracts/launch/launch-uri.md): the browser app's `?key=value` query, the
//! same pairs in its `#fragment`, or a `stencil://open?…` link, read into one struct the way
//! browser/js/core/launch/query.js reads it. Pure; every slice comes from the allocator given
//! (use an arena). Corpus: common/fixtures/deepLink/launchQuery.json; the words: uriKeys.zig.
const std = @import("std");
const keys = @import("uriKeys.zig");
const t = @import("uriText.zig");

pub const Server = struct { url: []const u8, id: []const u8, version: f64 };
/// `host[:port]/projects/<ref>` or a bare `<ref>` (id or name); a null server is the launch's, else local.
pub const Project = struct { server: ?[]const u8, ref: []const u8 };
pub const Source = struct {
    url: []const u8,
    frame: ?f64,
    /// The index `-f` takes; null when absent or negative.
    pub fn frameIndex(s: Source) ?u32 {
        const f = s.frame orelse return null;
        return if (f >= 0) @intFromFloat(@min(f, @as(f64, std.math.maxInt(u32)))) else null;
    }
};
pub const Blank = struct {
    color: []const u8,
    width: ?f64,
    height: ?f64,
    /// Both sides in px, capped as `--blank` caps them; null unless both are given.
    pub fn sizePx(b: Blank) ?[2]u32 {
        const w = b.width orelse return null;
        const h = b.height orelse return null;
        return .{ t.px(w), t.px(h) };
    }
};
pub const Picture = union(enum) { server: Server, project: Project, source: Source, blank: Blank };
/// `json` is the inline text, already checked to be one JSON object.
pub const Layout = union(enum) {
    url: []const u8,
    json: []const u8,
    /// What `-l` / `/layout` take: the URL, or the inline text itself.
    pub fn text(l: Layout) []const u8 {
        return switch (l) {
            .url => |u| u,
            .json => |j| j,
        };
    }
};
pub const Value = t.Value;
pub const Setting = struct { key: []const u8, value: Value };
pub const Connect = struct { url: []const u8, token: []const u8 };
pub const WarningCode = enum {
    secret_in_query,
    bad_json,
    bad_value,
    /// The corpus spelling.
    pub fn word(c: WarningCode) []const u8 {
        return switch (c) {
            .secret_in_query => "secret-in-query",
            .bad_json => "bad-json",
            .bad_value => "bad-value",
        };
    }
};
pub const Warning = struct { code: WarningCode, key: []const u8 };
pub const Llm = struct {
    provider: ?[]const u8 = null,
    model: ?[]const u8 = null,
    base_url: ?[]const u8 = null,
    server_url: ?[]const u8 = null,
    api_key: ?[]const u8 = null,
    pub fn any(l: Llm) bool {
        return l.provider != null or l.model != null or l.base_url != null or l.server_url != null or l.api_key != null;
    }
};

pub const LaunchUri = struct {
    picture: ?Picture = null,
    name: ?[]const u8 = null,
    layout: ?Layout = null,
    incognito: bool = false,
    script: []const u8 = "",
    theme: ?[]const u8 = null,
    settings: []Setting = &.{},
    llm: Llm = .{},
    chat_open: bool = false,
    chat_dock: ?[]const u8 = null,
    connect: []Connect = &.{},
    description: ?[]const u8 = null,
    keywords: ?[]const u8 = null,
    color: ?[]const u8 = null,
    expire: ?[]const u8 = null,
    warnings: []Warning = &.{},
    from_hash: bool = false,
    opens_session: bool = false,

    /// What a surface can act on: a picture URL carrying grammar-looking parameters is not a link.
    pub fn actionable(q: LaunchUri) bool {
        return q.opens_session or q.connect.len > 0 or q.layout != null or q.theme != null or q.llm.any() or
            q.chat_open or q.chat_dock != null;
    }
    /// A project ref's server: the locator's, else the launch's first connection, else none (a local project).
    pub fn projectOrigin(q: LaunchUri, server: ?[]const u8) ?[]const u8 {
        return server orelse (if (q.connect.len > 0) q.connect[0].url else null);
    }
};

const Kept = struct { canonical: []const u8, value: []const u8 };

/// `#stencil=` and `#stencil-desktop=` are the JSON hand-offs; any other `#a=b` is this grammar.
pub fn isLaunchQueryHash(hash: []const u8) bool {
    return hash.len > 0 and hash[0] == '#' and std.mem.indexOfScalar(u8, hash, '=') != null and
        !std.mem.startsWith(u8, hash, "#stencil=") and !std.mem.startsWith(u8, hash, "#stencil-desktop=");
}
fn isJsonObject(a: std.mem.Allocator, s: []const u8) bool {
    const v = std.json.parseFromSliceLeaky(std.json.Value, a, s, .{}) catch return false;
    return v == .object;
}
fn first(kept: []const Kept, key: []const u8) ?[]const u8 {
    for (kept) |k| if (std.mem.eql(u8, k.canonical, key)) return k.value;
    return null;
}

/// Null when the URL parts carry nothing of this grammar (a bare `?open=<id>` included).
pub fn parseParts(a: std.mem.Allocator, search: []const u8, hash: []const u8) !?LaunchUri {
    var pairs: std.ArrayList(t.Pair) = .empty;
    try t.pairsOf(a, if (search.len > 0 and search[0] == '?') search[1..] else search, &pairs);
    const from_hash = isLaunchQueryHash(hash);
    const query_len = pairs.items.len;
    if (from_hash) try t.pairsOf(a, hash[1..], &pairs);
    var any = false;
    for (pairs.items) |p| any = any or !std.mem.eql(u8, p.key, "open");
    if (!any) return null;

    var q = LaunchUri{ .from_hash = from_hash };
    var warnings: std.ArrayList(Warning) = .empty;
    var settings: std.ArrayList(Setting) = .empty;
    var kept: std.ArrayList(Kept) = .empty;
    for (pairs.items, 0..) |p, i| {
        const canonical = keys.canonicalOf(p.key) orelse {
            const key = try t.camel(a, p.key);
            var seen = key.len == 0 or keys.oneOf(key, &keys.unsafe_keys);
            for (settings.items) |s| seen = seen or std.mem.eql(u8, s.key, key);
            if (!seen) try settings.append(a, .{ .key = key, .value = try t.coerce(a, p.value) });
            continue;
        };
        if (keys.oneOf(canonical, &keys.secret_keys) and i < query_len) {
            try warnings.append(a, .{ .code = .secret_in_query, .key = canonical });
            continue;
        }
        try kept.append(a, .{ .canonical = canonical, .value = p.value });
    }
    const g = kept.items;

    const server = t.text(first(g, "server"));
    const id = t.text(first(g, "id"));
    if (server != null and id != null) {
        q.picture = .{ .server = .{ .url = server.?, .id = id.?, .version = t.numberOr(first(g, "version"), 0).? } };
    } else if (t.text(first(g, "project"))) |locator| {
        // `host[:port]/projects/<ref>` names the server too; `server=` supplies a missing one.
        const at = std.mem.indexOf(u8, locator, "/projects/");
        const origin = if (at) |i| std.mem.trimEnd(u8, locator[0..i], "/") else "";
        const ref = std.mem.trimEnd(u8, if (at) |i| locator[i + "/projects/".len ..] else locator, "/");
        if (ref.len == 0) try warnings.append(a, .{ .code = .bad_value, .key = "project" }) else q.picture = .{ .project = .{ .server = if (origin.len > 0) origin else server, .ref = ref } };
    } else if (t.text(first(g, "source"))) |src| {
        if (t.isHttp(src) or std.ascii.startsWithIgnoreCase(src, "data:")) {
            q.picture = .{ .source = .{ .url = src, .frame = t.numberOr(first(g, "frame"), null) } };
        } else try warnings.append(a, .{ .code = .bad_value, .key = "source" });
    } else if (first(g, "blank") != null) {
        q.picture = .{ .blank = .{ .color = t.text(first(g, "blank")) orelse "#ffffff", .width = t.sizeOr(first(g, "width")), .height = t.sizeOr(first(g, "height")) } };
    }
    q.name = t.text(first(g, "name"));
    if (t.text(first(g, "layout-raw"))) |raw| {
        if (isJsonObject(a, raw)) q.layout = .{ .json = raw } else try warnings.append(a, .{ .code = .bad_json, .key = "layout-raw" });
    } else if (t.text(first(g, "layout"))) |layout| {
        if (layout[0] == '{') {
            if (isJsonObject(a, layout)) q.layout = .{ .json = layout } else try warnings.append(a, .{ .code = .bad_json, .key = "layout" });
        } else if (t.isHttp(layout)) q.layout = .{ .url = layout } else try warnings.append(a, .{ .code = .bad_value, .key = "layout" });
    }
    q.incognito = if (first(g, "incognito")) |v| try t.flag(a, v) else false;
    const script = first(g, "script") orelse "";
    if (script.len > keys.scriptMaxChars(a)) try warnings.append(a, .{ .code = .bad_value, .key = "script" }) else q.script = script;
    if (first(g, "theme")) |v| {
        const word = try t.lower(a, v);
        for (keys.theme_words) |tw| if (std.mem.eql(u8, tw[0], word)) {
            q.theme = tw[1];
        };
        if (q.theme == null) try warnings.append(a, .{ .code = .bad_value, .key = "theme" });
    }
    if (t.text(first(g, "main-color"))) |mc| {
        var found = false;
        for (settings.items) |*s| if (std.mem.eql(u8, s.key, "mainTheme")) {
            s.value = .{ .text = mc };
            found = true;
        };
        if (!found) try settings.append(a, .{ .key = "mainTheme", .value = .{ .text = mc } });
    }
    q.description = t.text(first(g, "description"));
    q.keywords = t.text(first(g, "keywords"));
    q.color = t.text(first(g, "project-color"));
    q.expire = t.text(first(g, "expire"));
    q.llm = .{ .provider = t.text(first(g, "llm-provider")), .model = t.text(first(g, "llm-model")), .base_url = t.text(first(g, "llm-base-url")), .server_url = t.text(first(g, "llm-server-url")), .api_key = t.text(first(g, "llm-api-key")) };
    if (first(g, "chat")) |c| q.chat_open = (try t.flag(a, c)) or std.mem.eql(u8, try t.lower(a, c), "open");
    if (first(g, "chat-dock")) |d| {
        const dock = try t.lower(a, d);
        if (keys.oneOf(dock, &keys.chat_docks)) q.chat_dock = dock else try warnings.append(a, .{ .code = .bad_value, .key = "chat-dock" });
    }
    var connects: std.ArrayList(Connect) = .empty;
    var tokens: std.ArrayList([]const u8) = .empty;
    for (g) |k| if (std.mem.eql(u8, k.canonical, "token")) try tokens.append(a, k.value);
    var n: usize = 0;
    for (g) |k| if (std.mem.eql(u8, k.canonical, "connect")) {
        const token = if (n < tokens.items.len) tokens.items[n] else "";
        n += 1;
        if (t.text(k.value)) |url| try connects.append(a, .{ .url = url, .token = token });
    };
    q.connect = connects.items;
    q.settings = settings.items;
    q.warnings = warnings.items;
    q.opens_session = q.picture != null or q.incognito or q.script.len > 0;
    return q;
}

/// A whole URL: `http(s)://…?query#fragment` or `stencil://open?query`; null for any other text.
pub fn parse(a: std.mem.Allocator, url: []const u8) !?LaunchUri {
    const u = t.trimmed(url);
    if (!t.isHttp(u) and !std.ascii.startsWithIgnoreCase(u, "stencil:")) return null;
    const hash_at = std.mem.indexOfScalar(u8, u, '#');
    const head = if (hash_at) |h| u[0..h] else u;
    const query_at = std.mem.indexOfScalar(u8, head, '?');
    return parseParts(a, if (query_at) |qi| head[qi..] else "", if (hash_at) |h| u[h..] else "");
}
