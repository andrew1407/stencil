//! `/keywords`, `/keywords-search`, `/keywords-add` and `/keywords-del`: the keyword set on
//! server projects, addressed by NAME across every connection. Parsing lives in
//! keywordTargets.zig.
const std = @import("std");
const server = @import("../../serverClient.zig");
const logo = @import("../../logo.zig");
const msg = @import("../../messages.zig");
const Session = @import("../session.zig").Session;
const kwt = @import("keywordTargets.zig");
const KwMode = kwt.KwMode;
const containsIgnoreCase = kwt.containsIgnoreCase;
const printKeywordCsv = kwt.printKeywordCsv;
const printKeywords = kwt.printKeywords;
const splitTargetSpec = kwt.splitTargetSpec;
const collectTargetNames = kwt.collectTargetNames;


/// Version-guarded PUT of the keyword set with a 409 retry (a peer saved first → re-read the
/// version + retry), mirroring putProjectField.
fn putKeywords(client: *server.Client, id: []const u8, keywords: []const []const u8, version_in: i64) bool {
    var version = version_in;
    var tries: u8 = 0;
    while (tries < 4) : (tries += 1) {
        client.updateProjectKeywords(id, keywords, version) catch |e| {
            if (e == server.Error.Conflict) {
                if (client.getProjectVersion(id)) |v| {
                    version = v;
                    continue;
                } else |_| return false;
            }
            logo.err(msg.could_not_update_keywords, .{@errorName(e)});
            return false;
        };
        return true;
    }
    return false;
}

/// Resolve a project by name across connected servers, apply an add/del of `delta` keywords, PUT
/// the result version-guarded, and print the resulting set (or an error / not-found).
fn applyKeywordChange(session: *Session, name: []const u8, delta: []const []const u8, mode: KwMode) !void {
    var client: ?*server.Client = null;
    var ref: ?server.ProjectRef = null;
    for (session.servers.items) |*c| {
        const r = c.findProjectRef(name) catch |e| {
            logo.err(msg.could_not_query_server, .{ c.base, @errorName(e) });
            continue;
        };
        if (r) |rr| {
            client = c;
            ref = rr;
            break;
        }
    }
    if (client == null) {
        logo.print(msg.no_project_named_anywhere, .{name});
        return;
    }
    const cl = client.?;
    const rf = ref.?;
    defer session.gpa.free(rf.id);

    const current = cl.getProjectKeywords(rf.id) catch |e| {
        logo.err(msg.could_not_read_keywords, .{ name, @errorName(e) });
        return;
    };
    defer server.freeStrList(session.gpa, current);

    var next: std.ArrayList([]const u8) = .empty;
    defer next.deinit(session.gpa);
    if (mode == .add) {
        for (current) |k| try next.append(session.gpa, k);
        for (delta) |k| {
            var dup = false;
            for (next.items) |e| {
                if (std.ascii.eqlIgnoreCase(e, k)) {
                    dup = true;
                    break;
                }
            }
            if (!dup) try next.append(session.gpa, k);
        }
    } else {
        for (current) |k| {
            var drop = false;
            for (delta) |d| {
                if (std.ascii.eqlIgnoreCase(k, d)) {
                    drop = true;
                    break;
                }
            }
            if (!drop) try next.append(session.gpa, k);
        }
    }

    if (!putKeywords(cl, rf.id, next.items, rf.version)) return;
    printKeywords(name, next.items);
}

/// `/keywords <project | ["a","b"]>` — show one or more projects' keyword sets.
pub fn doKeywords(session: *Session, arg: []const u8) !void {
    if (session.servers.items.len == 0) {
        logo.print(msg.no_server_connections, .{});
        return;
    }
    if (std.mem.trim(u8, arg, " \t").len == 0) {
        logo.print(msg.keywords_usage, .{});
        return;
    }
    var names = try collectTargetNames(session.gpa, arg);
    defer names.deinit(session.gpa);
    for (names.items) |name| {
        var shown = false;
        for (session.servers.items) |*c| {
            const r = c.findProjectRef(name) catch continue;
            if (r) |rf| {
                defer session.gpa.free(rf.id);
                const kws = c.getProjectKeywords(rf.id) catch |e| {
                    logo.err(msg.could_not_read_keywords, .{ name, @errorName(e) });
                    shown = true;
                    break;
                };
                defer server.freeStrList(session.gpa, kws);
                printKeywords(name, kws);
                shown = true;
                break;
            }
        }
        if (!shown) logo.print(msg.no_project_named_anywhere, .{name});
    }
}

/// `/keywords-search <keyword...>` — list projects across servers whose keywords match any term
/// (case-insensitive substring).
pub fn doKeywordsSearch(session: *Session, arg: []const u8) !void {
    if (session.servers.items.len == 0) {
        logo.print(msg.no_server_connections, .{});
        return;
    }
    var terms: std.ArrayList([]const u8) = .empty;
    defer terms.deinit(session.gpa);
    var tit = std.mem.tokenizeAny(u8, arg, " \t,");
    while (tit.next()) |t| try terms.append(session.gpa, t);
    if (terms.items.len == 0) {
        logo.print(msg.keywords_search_usage, .{});
        return;
    }
    var found: usize = 0;
    for (session.servers.items) |*c| {
        const items = c.listProjectInfos() catch |e| {
            logo.err(msg.could_not_list_projects, .{ c.base, @errorName(e) });
            continue;
        };
        defer server.freeProjectList(session.gpa, items);
        for (items) |p| {
            var hit = false;
            for (p.keywords) |kw| {
                for (terms.items) |t| {
                    if (containsIgnoreCase(kw, t)) {
                        hit = true;
                        break;
                    }
                }
                if (hit) break;
            }
            if (hit) {
                found += 1;
                logo.print("  {s}  ({s}):", .{ p.name, c.base });
                printKeywordCsv(p.keywords);
                logo.print("\n", .{});
            }
        }
    }
    if (found == 0) logo.print(msg.no_keyword_matches, .{});
}

/// `/keywords-add <project | ["a","b"]> <keyword...>` — add keywords to one or more projects.
pub fn doKeywordsAdd(session: *Session, arg: []const u8) !void {
    try doKeywordsChange(session, arg, .add);
}

/// `/keywords-del <project | ["a","b"]> <keyword...>` — remove keywords from one or more projects.
pub fn doKeywordsDel(session: *Session, arg: []const u8) !void {
    try doKeywordsChange(session, arg, .del);
}

fn doKeywordsChange(session: *Session, arg: []const u8, mode: KwMode) !void {
    if (session.servers.items.len == 0) {
        logo.err(msg.no_server_connections, .{});
        return;
    }
    const split = splitTargetSpec(arg);
    var names = try collectTargetNames(session.gpa, split.target);
    defer names.deinit(session.gpa);
    var delta: std.ArrayList([]const u8) = .empty;
    defer delta.deinit(session.gpa);
    var dit = std.mem.tokenizeAny(u8, split.rest, " \t,");
    while (dit.next()) |k| try delta.append(session.gpa, k);
    if (names.items.len == 0 or delta.items.len == 0) {
        const verb = if (mode == .add) "add" else "del";
        logo.print(msg.keywords_edit_usage, .{verb});
        return;
    }
    for (names.items) |name| try applyKeywordChange(session, name, delta.items, mode);
}
