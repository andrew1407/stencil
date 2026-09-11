//! Registry-driven op-plan schema (contract §1–§2, §11): the cli port of
//! browser/js/llm/opSchema.js over the embedded opRegistry.json. Generic checks only —
//! profile membership, unknown keys, required/types/enums/ranges/caps/grammars and the
//! cross-field rules (forms / together / exclusive / minFields / onlyWith /
//! requiredWith). opplan.zig keeps the typed normalizers, executors and cli extras.
const std = @import("std");

/// The canonical registry (browser/js/config/llm/opRegistry.json), embedded whole.
pub const registry_json = @embedFile("opRegistry.json");
pub const surface = "cli";
pub const profile = "console";

pub const Value = std.json.Value;
pub const ObjectMap = std.json.ObjectMap;
pub const Error = error{ Invalid, OutOfMemory };

/// Collects the first failure's user-facing message (gpa-owned). `prefix` is the entry
/// point's ("invalid crop action: " / "invalid plan: ").
pub const Diag = struct {
    gpa: std.mem.Allocator,
    msg: ?[]u8 = null,
    prefix: []const u8 = "",

    pub fn fail(self: *Diag, comptime fmt: []const u8, args: anytype) Error {
        if (self.msg == null) self.msg = try std.fmt.allocPrint(self.gpa, "{s}" ++ fmt, .{self.prefix} ++ args);
        return error.Invalid;
    }

    pub fn deinit(self: *Diag) void {
        if (self.msg) |m| self.gpa.free(m);
        self.msg = null;
    }
};

/// One op this surface registers, resolved for it (surfaceKeys / bulletVariants /
/// surfaceFlags applied).
pub const Entry = struct {
    name: []const u8,
    raw: ObjectMap, // the registry entry: forms / together / exclusive / minFields / rules
    keys: ObjectMap,
    rules: []const []const u8,
    bullet: ?[]const u8,
    addendum: ?[]const u8, // the console `{addendum}` line riding after the bullets
    top_level_only: bool,
    settings: bool, // editorSetting | consoleSetting — never inside variants (§10)
    deferred: bool,
};

pub const OpsetHit = union(enum) { entry: Entry, fail, none };

// the token grammars (registry.regexes), hand-matched — Zig has no regex

pub const Grammar = enum { CROP_TOKEN, CROP_ASPECT, PAGE_FORMAT, HEX, CSS_NAME, FORMULA_X, FORMULA_Y, HTTP_URL, URL_SCHEME };

pub fn matches(g: Grammar, s: []const u8) bool {
    return switch (g) {
        .CROP_TOKEN => isCropToken(s),
        .CROP_ASPECT => isAspectToken(s),
        .PAGE_FORMAT => isPageFormat(s),
        .HEX => isHex6(s),
        .CSS_NAME => s.len != 0 and allAlpha(s),
        .FORMULA_X => isFormula(s, 'x'),
        .FORMULA_Y => isFormula(s, 'y'),
        .HTTP_URL => isHttpUrl(s),
        .URL_SCHEME => hasUrlScheme(s),
    };
}

fn grammar(name: []const u8) Grammar {
    return std.meta.stringToEnum(Grammar, name) orelse std.debug.panic("opRegistry: unknown grammar \"{s}\"", .{name});
}

/// `^-?(\d+(\.\d+)?|\.\d+)(%|px|cm|in)?$`
fn isCropToken(token: []const u8) bool {
    const rest = if (std.mem.startsWith(u8, token, "-")) token[1..] else token;
    var i: usize = 0;
    while (i < rest.len and (std.ascii.isDigit(rest[i]) or rest[i] == '.')) : (i += 1) {}
    const number = rest[0..i];
    const unit = rest[i..];
    const valid_number = blk: {
        if (std.mem.indexOfScalar(u8, number, '.')) |dot| {
            const int = number[0..dot];
            const frac = number[dot + 1 ..];
            break :blk frac.len != 0 and allDigits(frac) and allDigits(int);
        }
        break :blk number.len != 0 and allDigits(number);
    };
    const valid_unit = unit.len == 0 or std.mem.eql(u8, unit, "%") or
        std.mem.eql(u8, unit, "px") or std.mem.eql(u8, unit, "cm") or std.mem.eql(u8, unit, "in");
    return valid_number and valid_unit;
}

fn allDigits(s: []const u8) bool {
    for (s) |c| {
        if (!std.ascii.isDigit(c)) return false;
    }
    return true;
}

/// `^0*[1-9]\d*:0*[1-9]\d*$` — both sides > 0.
fn isAspectToken(token: []const u8) bool {
    const colon = std.mem.indexOfScalar(u8, token, ':') orelse return false;
    const w = token[0..colon];
    const h = token[colon + 1 ..];
    if (w.len == 0 or h.len == 0 or !allDigits(w) or !allDigits(h)) return false;
    return std.mem.indexOfNone(u8, w, "0") != null and std.mem.indexOfNone(u8, h, "0") != null;
}

/// `^[abc](10|[0-9])$`
fn isPageFormat(format: []const u8) bool {
    if (format.len < 2) return false;
    const series = format[0];
    if (series != 'a' and series != 'b' and series != 'c') return false;
    const number = format[1..];
    if (std.mem.eql(u8, number, "10")) return true;
    return number.len == 1 and std.ascii.isDigit(number[0]);
}

/// `^#[0-9a-fA-F]{6}$`
fn isHex6(color: []const u8) bool {
    if (color.len != 7 or color[0] != '#') return false;
    for (color[1..]) |c| {
        if (!std.ascii.isHex(c)) return false;
    }
    return true;
}

fn allAlpha(s: []const u8) bool {
    for (s) |c| {
        if (!std.ascii.isAlphabetic(c)) return false;
    }
    return true;
}

/// `^[0-9x+\-*/(). ]+$` (or y): the single variable matching the axis.
fn isFormula(s: []const u8, axis: u8) bool {
    if (s.len == 0) return false;
    for (s) |c| {
        if (!(std.ascii.isDigit(c) or c == axis or std.mem.indexOfScalar(u8, "+-*/(). ", c) != null)) return false;
    }
    return true;
}

/// `^[hH][tT][tT][pP][sS]?://\S+$`
fn isHttpUrl(url: []const u8) bool {
    const scheme_len: usize = if (std.ascii.startsWithIgnoreCase(url, "https://"))
        "https://".len
    else if (std.ascii.startsWithIgnoreCase(url, "http://"))
        "http://".len
    else
        return false;
    if (url.len == scheme_len) return false;
    return !hasWhitespace(url);
}

/// JS `\s`: ASCII whitespace plus the Unicode spaces and line separators.
fn hasWhitespace(s: []const u8) bool {
    var it = std.unicode.Utf8View.initUnchecked(s).iterator();
    while (it.nextCodepoint()) |cp| {
        const ws = switch (cp) {
            0x09...0x0D, 0x20, 0xA0, 0x1680, 0x2000...0x200A, 0x2028, 0x2029, 0x202F, 0x205F, 0x3000, 0xFEFF => true,
            else => false,
        };
        if (ws) return true;
    }
    return false;
}

/// `^[a-zA-Z][a-zA-Z0-9+.\-]*://` — a prefix match.
fn hasUrlScheme(s: []const u8) bool {
    if (s.len == 0 or !std.ascii.isAlphabetic(s[0])) return false;
    var i: usize = 1;
    while (i < s.len and (std.ascii.isAlphanumeric(s[i]) or s[i] == '+' or s[i] == '.' or s[i] == '-')) : (i += 1) {}
    return std.mem.startsWith(u8, s[i..], "://");
}

/// A finite number: integers, finite floats, and the overflowed literals std.json
/// keeps as text (JSON.parse reads those as finite floats too).
fn numOf(v: Value) ?f64 {
    return switch (v) {
        .integer => |i| @floatFromInt(i),
        .float => |f| if (std.math.isFinite(f)) f else null,
        .number_string => |s| blk: {
            const f = std.fmt.parseFloat(f64, s) catch break :blk null;
            break :blk if (std.math.isFinite(f)) f else null;
        },
        else => null,
    };
}

fn isInt(v: Value) bool {
    const f = numOf(v) orelse return false;
    return @floor(f) == f;
}

fn present(obj: ObjectMap, key: []const u8) bool {
    const v = obj.get(key) orelse return false;
    return v != .null;
}

fn getObj(o: ObjectMap, key: []const u8) ?ObjectMap {
    const v = o.get(key) orelse return null;
    return if (v == .object) v.object else null;
}

fn getArr(o: ObjectMap, key: []const u8) ?[]Value {
    const v = o.get(key) orelse return null;
    return if (v == .array) v.array.items else null;
}

fn getStr(o: ObjectMap, key: []const u8) ?[]const u8 {
    const v = o.get(key) orelse return null;
    return if (v == .string) v.string else null;
}

fn getBool(o: ObjectMap, key: []const u8) bool {
    const v = o.get(key) orelse return false;
    return v == .bool and v.bool;
}

fn strEq(v: Value, s: []const u8) bool {
    return v == .string and std.mem.eql(u8, v.string, s);
}

/// JS SameValueZero over the JSON primitives.
fn valueEq(x: Value, y: Value) bool {
    switch (x) {
        .string => |s| return strEq(y, s),
        .bool => |b| return y == .bool and y.bool == b,
        .integer, .float, .number_string => {
            const fx = numOf(x) orelse return false;
            const fy = numOf(y) orelse return false;
            return fx == fy;
        },
        else => return false,
    }
}

fn inList(v: Value, list: Value) bool {
    if (list != .array) return false;
    for (list.array.items) |x| {
        if (valueEq(v, x)) return true;
    }
    return false;
}

fn trimWs(s: []const u8) []const u8 {
    return std.mem.trim(u8, s, &std.ascii.whitespace);
}

// message plumbing: where a value sits

const Path = struct { root: []const u8 = "", key: []const u8 = "", container: ?[]const u8 = null };

const Ctx = struct {
    a: std.mem.Allocator, // scratch for messages and folded copies
    diag: *Diag,
    schema: *const Schema,

    fn fail(self: Ctx, comptime fmt: []const u8, args: anytype) Error {
        return self.diag.fail(fmt, args);
    }
};

/// `spec`, `ask.options[2]`, `lines[0].points[3]`.
fn where(ctx: Ctx, p: Path) Error![]const u8 {
    if (p.key.len == 0) return std.mem.trimEnd(u8, p.root, ".");
    return std.fmt.allocPrint(ctx.a, "{s}{s}{s}{s}", .{ p.container orelse "", if (p.container != null) "." else "", p.root, p.key });
}

/// `"x1" in spec`, `"ask.question"`.
fn label(ctx: Ctx, p: Path) Error![]const u8 {
    if (p.container) |c| return std.fmt.allocPrint(ctx.a, "\"{s}{s}\" in {s}", .{ p.root, p.key, c });
    return std.fmt.allocPrint(ctx.a, "\"{s}{s}\"", .{ p.root, p.key });
}

fn child(ctx: Ctx, p: ?Path, key: []const u8) Error!Path {
    if (p) |parent| {
        if (parent.key.len != 0) return .{ .key = key, .container = try where(ctx, parent) };
        return .{ .root = parent.root, .key = key };
    }
    return .{ .key = key };
}

fn item(ctx: Ctx, p: Path, i: usize) Error!Path {
    return .{ .root = p.root, .key = try std.fmt.allocPrint(ctx.a, "{s}[{d}]", .{ p.key, i }), .container = p.container };
}

fn numText(ctx: Ctx, v: Value) Error![]const u8 {
    return switch (v) {
        .integer => |i| std.fmt.allocPrint(ctx.a, "{d}", .{i}),
        .float => |f| std.fmt.allocPrint(ctx.a, "{d}", .{f}),
        .number_string => |s| s,
        else => "?",
    };
}

/// `"a", "b"` / `1, 2` / `true`.
fn quoteList(ctx: Ctx, list: Value) Error![]const u8 {
    var out: std.ArrayList(u8) = .empty;
    for (list.array.items, 0..) |x, i| {
        if (i != 0) try out.appendSlice(ctx.a, ", ");
        switch (x) {
            .string => |s| try out.print(ctx.a, "\"{s}\"", .{s}),
            .bool => |b| try out.appendSlice(ctx.a, if (b) "true" else "false"),
            else => try out.appendSlice(ctx.a, try numText(ctx, x)),
        }
    }
    return out.toOwnedSlice(ctx.a);
}

fn quoteKeys(ctx: Ctx, keys: []const Value, sep: []const u8) Error![]const u8 {
    var out: std.ArrayList(u8) = .empty;
    for (keys, 0..) |k, i| {
        if (i != 0) try out.appendSlice(ctx.a, sep);
        try out.print(ctx.a, "\"{s}\"", .{k.string});
    }
    return out.toOwnedSlice(ctx.a);
}

// native cross-field rules an entry may name in `rules`

/// §3.2 tolerance: "aspect" beside "spec" folds into the spec when it lacks one; a
/// conflicting duplicate fails. The folded copy is what gets validated + normalized.
fn cropAspectFold(ctx: Ctx, a: ObjectMap) Error!ObjectMap {
    const beside = a.get("aspect") orelse return a;
    if (beside == .null) return a;
    const spec_v = a.get("spec") orelse return a;
    if (spec_v != .object) return a;
    var spec: ObjectMap = .empty;
    var it = spec_v.object.iterator();
    while (it.next()) |kv| try spec.put(ctx.a, kv.key_ptr.*, kv.value_ptr.*);
    if (present(spec, "aspect")) {
        if (!valueEq(spec.get("aspect").?, beside)) return ctx.fail("\"aspect\" appears both beside \"spec\" and inside it with different values", .{});
    } else try spec.put(ctx.a, "aspect", beside);
    var out: ObjectMap = .empty;
    var ait = a.iterator();
    while (ait.next()) |kv| {
        if (std.mem.eql(u8, kv.key_ptr.*, "aspect")) continue;
        try out.put(ctx.a, kv.key_ptr.*, if (std.mem.eql(u8, kv.key_ptr.*, "spec")) Value{ .object = spec } else kv.value_ptr.*);
    }
    return out;
}

// the checks (opSchema.js order: rules → unknown keys → presence rules → per key)

fn checkString(ctx: Ctx, v: Value, spec: ObjectMap, path: Path, parent: ?ObjectMap) Error!void {
    if (v != .string) return ctx.fail("{s} must be a string", .{try label(ctx, path)});
    const max = if (spec.get("maxChars")) |m| ctx.schema.limit(m) else ctx.schema.limitNamed("MAX_STRING_CHARS");
    if (@as(f64, @floatFromInt(v.string.len)) > max) return ctx.fail("{s} is longer than {d} characters", .{ try label(ctx, path), @as(u64, @intFromFloat(max)) });
    const s = if (getBool(spec, "trim")) trimWs(v.string) else v.string;
    if (getBool(spec, "nonEmpty") and trimWs(s).len == 0) return ctx.fail("{s} must be a non-empty string", .{try label(ctx, path)});
    if (spec.get("enum")) |e| {
        if (!inList(.{ .string = s }, e)) return ctx.fail("{s} must be one of {s}", .{ try label(ctx, path), try quoteList(ctx, e) });
    }
    if (spec.get("literals")) |l| {
        if (inList(.{ .string = s }, l)) return;
    }
    if (getBool(spec, "blankOk") and trimWs(s).len == 0) return;
    var names: [8][]const u8 = undefined;
    var n: usize = 0;
    if (getObj(spec, "regexBy")) |by| {
        // The grammar depends on a sibling's value (formula `expr` by `axis`).
        const map = getObj(by, "map").?;
        if (parent) |p| {
            if (p.get(getStr(by, "key").?)) |dep| {
                if (dep == .string) {
                    if (getStr(map, dep.string)) |g| {
                        names[0] = g;
                        n = 1;
                    }
                }
            }
        }
    } else if (spec.get("regex")) |r| switch (r) {
        .string => |g| {
            names[0] = g;
            n = 1;
        },
        .array => |arr| for (arr.items) |g| {
            names[n] = g.string;
            n += 1;
        },
        else => {},
    };
    if (n != 0) {
        var hit = false;
        for (names[0..n]) |g| hit = hit or matches(grammar(g), s);
        if (!hit) {
            var wording: std.ArrayList(u8) = .empty;
            for (names[0..n], 0..) |g, i| {
                if (i != 0) try wording.appendSlice(ctx.a, " or ");
                try wording.appendSlice(ctx.a, ctx.schema.describe(g));
            }
            return ctx.fail("{s} must be {s}", .{ try label(ctx, path), wording.items });
        }
    }
    if (getStr(spec, "regexNot")) |g| {
        if (matches(grammar(g), s)) return ctx.fail("{s} must be a local value, not {s}", .{ try label(ctx, path), ctx.schema.describe(g) });
    }
}

fn checkNumber(ctx: Ctx, v: Value, spec: ObjectMap, path: Path) Error!void {
    const integer = strEq(spec.get("type").?, "integer");
    const noun: []const u8 = if (integer) "an integer" else "a number";
    if (!(if (integer) isInt(v) else numOf(v) != null)) return ctx.fail("{s} must be {s}", .{ try label(ctx, path), noun });
    if (spec.get("enum")) |e| {
        if (!inList(v, e)) return ctx.fail("{s} must be one of {s}", .{ try label(ctx, path), try quoteList(ctx, e) });
    }
    if (getArr(spec, "range")) |r| {
        const f = numOf(v).?;
        const lo = numOf(r[0]);
        const hi = numOf(r[1]);
        if ((lo != null and f < lo.?) or (hi != null and f > hi.?)) {
            const range = if (lo != null and hi != null)
                try std.fmt.allocPrint(ctx.a, "{s}..{s}", .{ try numText(ctx, r[0]), try numText(ctx, r[1]) })
            else if (lo != null)
                try std.fmt.allocPrint(ctx.a, ">= {s}", .{try numText(ctx, r[0])})
            else
                try std.fmt.allocPrint(ctx.a, "<= {s}", .{try numText(ctx, r[1])});
            return ctx.fail("{s} must be {s} {s}", .{ try label(ctx, path), noun, range });
        }
    }
}

fn checkBoolean(ctx: Ctx, v: Value, spec: ObjectMap, path: Path) Error!void {
    if (v != .bool) return ctx.fail("{s} must be a boolean", .{try label(ctx, path)});
    if (spec.get("enum")) |e| {
        if (!inList(v, e)) return ctx.fail("{s} must be {s}", .{ try label(ctx, path), try quoteList(ctx, e) });
    }
}

fn checkArray(ctx: Ctx, v: Value, spec: ObjectMap, path: Path) Error!void {
    if (v != .array) return ctx.fail("{s} must be an array", .{try label(ctx, path)});
    const len: f64 = @floatFromInt(v.array.items.len);
    const min: ?f64 = if (spec.get("minItems")) |m| ctx.schema.limit(m) else null;
    const max: ?f64 = if (spec.get("maxItems")) |m| ctx.schema.limit(m) else null;
    if (min != null and min.? == 1 and len == 0) return ctx.fail("{s} must be a non-empty array", .{try label(ctx, path)});
    const window = min != null and min.? > 1 and max != null; // a real N..M window, not just a cap
    if (max != null and len > max.?) {
        if (window) return ctx.fail("{s} must hold {d}..{d} entries", .{ try label(ctx, path), @as(u64, @intFromFloat(min.?)), @as(u64, @intFromFloat(max.?)) });
        return ctx.fail("more than {d} entries in {s}", .{ @as(u64, @intFromFloat(max.?)), try label(ctx, path) });
    }
    if (min != null and len < min.?) {
        if (window) return ctx.fail("{s} must hold {d}..{d} entries", .{ try label(ctx, path), @as(u64, @intFromFloat(min.?)), @as(u64, @intFromFloat(max.?)) });
        return ctx.fail("{s} must hold at least {d} entries", .{ try label(ctx, path), @as(u64, @intFromFloat(min.?)) });
    }
    if (getObj(spec, "items")) |items| {
        for (v.array.items, 0..) |x, i| try checkValue(ctx, x, items, try item(ctx, path, i), null);
    }
}

fn checkObject(ctx: Ctx, v: Value, spec: ObjectMap, path: Path) Error!void {
    if (v != .object) return ctx.fail("{s} must be an object", .{try label(ctx, path)});
    if (spec.get("fields") != null or spec.get("minFields") != null)
        try checkFields(ctx, v.object, getObj(spec, "fields") orelse .empty, spec, path, &.{});
}

fn checkValue(ctx: Ctx, v: Value, spec: ObjectMap, path: Path, parent: ?ObjectMap) Error!void {
    const t = getStr(spec, "type") orelse "";
    if (std.mem.eql(u8, t, "string")) return checkString(ctx, v, spec, path, parent);
    if (std.mem.eql(u8, t, "integer") or std.mem.eql(u8, t, "number")) return checkNumber(ctx, v, spec, path);
    if (std.mem.eql(u8, t, "boolean")) return checkBoolean(ctx, v, spec, path);
    if (std.mem.eql(u8, t, "array")) return checkArray(ctx, v, spec, path);
    if (std.mem.eql(u8, t, "object")) return checkObject(ctx, v, spec, path);
    std.debug.panic("opRegistry: unknown type \"{s}\"", .{t});
}

/// One object against a key map + its holder's presence rules. `skip` names keys
/// that are neither declared nor unknown (the action's own "op").
fn checkFields(ctx: Ctx, obj: ObjectMap, fields: ObjectMap, holder: ObjectMap, path: ?Path, skip: []const []const u8) Error!void {
    // `allowUnknown` (the envelope's variant objects) tolerates undeclared keys.
    outer: for (obj.keys()) |k| {
        if (getBool(holder, "allowUnknown")) break;
        for (skip) |s| {
            if (std.mem.eql(u8, k, s)) continue :outer;
        }
        if (fields.get(k) == null) {
            if (path) |p| return ctx.fail("unknown field \"{s}\" in {s}", .{ k, try where(ctx, p) });
            return ctx.fail("unknown field \"{s}\"", .{k});
        }
    }
    if (getArr(holder, "forms")) |forms| {
        // The present keys among those the forms mention must equal exactly one group.
        var given: std.ArrayList([]const u8) = .empty;
        for (fields.keys()) |k| {
            var mentioned = false;
            for (forms) |f| for (f.array.items) |fk| {
                if (std.mem.eql(u8, fk.string, k)) mentioned = true;
            };
            if (mentioned and present(obj, k)) try given.append(ctx.a, k);
        }
        var matched: usize = 0;
        for (forms) |f| {
            if (f.array.items.len != given.items.len) continue;
            var all = true;
            for (f.array.items) |fk| {
                var found = false;
                for (given.items) |g| found = found or std.mem.eql(u8, fk.string, g);
                all = all and found;
            }
            if (all) matched += 1;
        }
        if (matched != 1) {
            var wording: std.ArrayList(u8) = .empty;
            for (forms, 0..) |f, i| {
                if (i != 0) try wording.appendSlice(ctx.a, " / ");
                try wording.appendSlice(ctx.a, try quoteKeys(ctx, f.array.items, "+"));
            }
            return ctx.fail("exactly one of {s} is required", .{wording.items});
        }
    }
    if (getArr(holder, "together")) |groups| for (groups) |g| {
        var n: usize = 0;
        for (g.array.items) |k| {
            if (present(obj, k.string)) n += 1;
        }
        if (n != 0 and n != g.array.items.len) return ctx.fail("{s} ride together", .{try quoteKeys(ctx, g.array.items, " and ")});
    };
    if (getArr(holder, "exclusive")) |groups| for (groups) |g| {
        var n: usize = 0;
        for (g.array.items) |k| {
            if (present(obj, k.string)) n += 1;
        }
        if (n > 1) return ctx.fail("carries both {s} — at most one of them", .{try quoteKeys(ctx, g.array.items, " and ")});
    };
    if (holder.get("minFields")) |m| {
        const min = numOf(m).?;
        var n: f64 = 0;
        for (fields.keys()) |k| {
            if (present(obj, k)) n += 1;
        }
        if (n < min) {
            var names: std.ArrayList(u8) = .empty;
            for (fields.keys(), 0..) |k, i| {
                if (i != 0) try names.append(ctx.a, '/');
                try names.appendSlice(ctx.a, k);
            }
            if (min == 1) return ctx.fail("needs at least one of {s}", .{names.items});
            return ctx.fail("needs at least {s} of {s}", .{ try numText(ctx, m), names.items });
        }
    }
    var it = fields.iterator();
    while (it.next()) |kv| {
        const k = kv.key_ptr.*;
        const spec = kv.value_ptr.object;
        const at = try child(ctx, path, k);
        if (!present(obj, k)) {
            if (getBool(spec, "required")) return ctx.fail("{s} is required", .{try label(ctx, at)});
            if (getObj(spec, "requiredWith")) |deps| {
                var dit = deps.iterator();
                while (dit.next()) |d| {
                    const dep_v = obj.get(d.key_ptr.*) orelse continue;
                    if (inList(dep_v, d.value_ptr.*)) {
                        var one = [_]Value{dep_v};
                        return ctx.fail("{s} is required with \"{s}\" {s}", .{ try label(ctx, at), d.key_ptr.*, try quoteList(ctx, .{ .array = std.json.Array.fromOwnedSlice(ctx.a, &one) }) });
                    }
                }
            }
            continue;
        }
        if (getObj(spec, "onlyWith")) |deps| {
            var dit = deps.iterator();
            while (dit.next()) |d| {
                const dep_v = obj.get(d.key_ptr.*) orelse Value.null;
                if (!inList(dep_v, d.value_ptr.*))
                    return ctx.fail("{s} only applies with \"{s}\" {s}", .{ try label(ctx, at), d.key_ptr.*, try quoteKeys(ctx, d.value_ptr.array.items, " or ") });
            }
        }
        try checkValue(ctx, obj.get(k).?, spec, at, obj);
    }
}

// normalization: the declared keys only, defaults applied, trims honoured

fn pick(a: std.mem.Allocator, v: Value, spec: ObjectMap) Error!Value {
    const t = getStr(spec, "type") orelse "";
    if (std.mem.eql(u8, t, "object") and v == .object) {
        if (getObj(spec, "fields")) |fields| return .{ .object = try pickFields(a, v.object, fields) };
    }
    if (std.mem.eql(u8, t, "array") and v == .array) {
        var out = std.json.Array.init(a);
        if (getObj(spec, "items")) |items| {
            for (v.array.items) |x| try out.append(try pick(a, x, items));
        } else try out.appendSlice(v.array.items);
        return .{ .array = out };
    }
    if (std.mem.eql(u8, t, "string") and getBool(spec, "trim") and v == .string) return .{ .string = trimWs(v.string) };
    return v;
}

fn pickFields(a: std.mem.Allocator, obj: ObjectMap, fields: ObjectMap) Error!ObjectMap {
    var out: ObjectMap = .empty;
    var it = fields.iterator();
    while (it.next()) |kv| {
        const k = kv.key_ptr.*;
        if (present(obj, k)) {
            try out.put(a, k, try pick(a, obj.get(k).?, kv.value_ptr.object));
        } else if (kv.value_ptr.object.get("default")) |d| try out.put(a, k, d);
    }
    return out;
}

// the schema: the registry resolved for this surface

pub const Schema = struct {
    root: ObjectMap,
    limits: ObjectMap,
    regexes: ObjectMap,
    entries: []const Entry,
    forbidden: []const []const u8,
    ask: ObjectMap, // the §11 card's key schema (ask.schema)
    default_custom_label: []const u8,
    envelope: ObjectMap,

    pub fn find(self: *const Schema, op: []const u8) ?*const Entry {
        for (self.entries) |*e| {
            if (std.mem.eql(u8, e.name, op)) return e;
        }
        return null;
    }

    pub fn isForbidden(self: *const Schema, op: []const u8) bool {
        for (self.forbidden) |name| {
            if (std.mem.eql(u8, name, op)) return true;
        }
        return false;
    }

    /// A cap is a number or a dotted name into `limits` ("MAX_ACTIONS", "ask.label").
    pub fn limit(self: *const Schema, v: Value) f64 {
        return switch (v) {
            .string => |name| self.limitNamed(name),
            else => numOf(v) orelse std.debug.panic("opRegistry: bad limit", .{}),
        };
    }

    pub fn limitNamed(self: *const Schema, name: []const u8) f64 {
        var cur: Value = .{ .object = self.limits };
        var it = std.mem.splitScalar(u8, name, '.');
        while (it.next()) |part| {
            if (cur != .object) break;
            cur = cur.object.get(part) orelse .null;
        }
        return numOf(cur) orelse std.debug.panic("opRegistry: unknown limit \"{s}\"", .{name});
    }

    pub fn describe(self: *const Schema, g: []const u8) []const u8 {
        const d = getObj(self.regexes, "describe") orelse return g;
        return getStr(d, g) orelse g;
    }

    /// Validate one action against its entry (native rules first). Returns the action
    /// as validated (post-fold) — feed it to `normalize`. Fails with "invalid <op> action: …".
    pub fn validateAction(self: *const Schema, a: std.mem.Allocator, diag: *Diag, action: ObjectMap, entry: *const Entry) Error!ObjectMap {
        diag.prefix = try std.fmt.allocPrint(a, "invalid {s} action: ", .{entry.name});
        const ctx = Ctx{ .a = a, .diag = diag, .schema = self };
        var v = action;
        for (entry.rules) |rule| {
            if (std.mem.eql(u8, rule, "cropAspectFold")) {
                v = try cropAspectFold(ctx, v);
            } else std.debug.panic("opRegistry: unknown native rule \"{s}\"", .{rule});
        }
        try checkFields(ctx, v, entry.keys, entry.raw, null, &.{"op"});
        return v;
    }

    /// The declared keys present (deep-picked, trims applied) plus defaults.
    pub fn normalize(_: *const Schema, a: std.mem.Allocator, v: ObjectMap, entry: *const Entry) Error!ObjectMap {
        return pickFields(a, v, entry.keys);
    }

    /// The §11 card's structure (option `actions` only shallowly — the caller validates
    /// them as nested actions). Fails with "invalid plan: …".
    pub fn validateAsk(self: *const Schema, a: std.mem.Allocator, diag: *Diag, ask: Value) Error!void {
        diag.prefix = "invalid plan: ";
        const ctx = Ctx{ .a = a, .diag = diag, .schema = self };
        if (ask != .object) return ctx.fail("\"ask\" must be an object", .{});
        try checkFields(ctx, ask.object, getObj(self.ask, "keys").?, self.ask, .{ .root = "ask." }, &.{});
    }

    pub fn normalizeAsk(self: *const Schema, a: std.mem.Allocator, ask: ObjectMap) Error!ObjectMap {
        return pickFields(a, ask, getObj(self.ask, "keys").?);
    }

    /// Check one envelope slot ("actions" / "variants") shallowly, labelled `as`
    /// ("actions", "variant 2", "ask option 1"). Fails with "invalid plan: …".
    pub fn checkEnvelope(self: *const Schema, a: std.mem.Allocator, diag: *Diag, v: Value, key: []const u8, as: []const u8) Error!void {
        diag.prefix = "invalid plan: ";
        const ctx = Ctx{ .a = a, .diag = diag, .schema = self };
        try checkValue(ctx, v, getObj(self.envelope, key).?, .{ .key = as }, null);
    }

    /// Resolve an op inside a nested op set (§8 open.actions): an entry to validate
    /// with, `fail` for a listed-but-disallowed op, or `none` for an unknown one.
    pub fn opsetEntry(self: *const Schema, name: []const u8, op: []const u8) OpsetHit {
        const os = getObj(getObj(self.root, "opsets").?, name) orelse std.debug.panic("opRegistry: unknown opset \"{s}\"", .{name});
        if (getObj(os, "overrides")) |ov| {
            if (getObj(ov, op)) |o| return .{ .entry = .{
                .name = op,
                .raw = o,
                .keys = getObj(o, "keys").?,
                .rules = &.{},
                .bullet = null,
                .addendum = null,
                .top_level_only = false,
                .settings = false,
                .deferred = false,
            } };
        }
        for (getArr(os, "ops").?) |x| {
            if (strEq(x, op)) {
                for (getArr(self.root, "ops").?) |e| {
                    if (strEq(e.object.get("id").?, op)) return .{ .entry = resolveEntry(e.object) catch return .none };
                }
                return .none;
            }
        }
        if (getArr(os, "failOps")) |fo| for (fo) |x| {
            if (strEq(x, op)) return .fail;
        };
        return .none;
    }
};

/// `map[surface] ?? map[profile]`.
fn forSurface(map: ?ObjectMap) ?Value {
    const m = map orelse return null;
    return m.get(surface) orelse m.get(profile);
}

fn resolveEntry(e: ObjectMap) error{OutOfMemory}!Entry {
    var flags: ObjectMap = .empty;
    if (getObj(e, "flags")) |f| {
        var it = f.iterator();
        while (it.next()) |kv| try flags.put(arena.allocator(), kv.key_ptr.*, kv.value_ptr.*);
    }
    if (forSurface(getObj(e, "surfaceFlags"))) |sf| {
        var it = sf.object.iterator();
        while (it.next()) |kv| try flags.put(arena.allocator(), kv.key_ptr.*, kv.value_ptr.*);
    }
    var rules: std.ArrayList([]const u8) = .empty;
    if (getArr(e, "rules")) |rs| for (rs) |r| try rules.append(arena.allocator(), r.string);
    var bullet = getStr(e, "bullet");
    var addendum: ?[]const u8 = null;
    if (forSurface(getObj(e, "bulletVariants"))) |variant| switch (variant) {
        .string => |s| bullet = s,
        .object => |o| addendum = getStr(o, "addendum"),
        else => {},
    };
    var keys = getObj(e, "keys").?;
    if (getObj(e, "surfaceKeys")) |sk| {
        if (getObj(sk, surface)) |k| keys = k;
    }
    return .{
        .name = getStr(e, "name").?,
        .raw = e,
        .keys = keys,
        .rules = try rules.toOwnedSlice(arena.allocator()),
        .bullet = bullet,
        .addendum = addendum,
        .top_level_only = getBool(flags, "topLevelOnly"),
        .settings = getBool(flags, "editorSetting") or getBool(flags, "consoleSetting"),
        .deferred = getBool(flags, "deferred"),
    };
}

// Parsed once on first use, from the main thread only (scrape's fetch pool never reaches
// here). The tree lives for the process.
var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
var instance: ?Schema = null;

fn build() error{OutOfMemory}!Schema {
    const a = arena.allocator();
    const root = std.json.parseFromSliceLeaky(Value, a, registry_json, .{}) catch @panic("embedded opRegistry.json is malformed");
    const r = root.object;
    const profiles = getObj(r, "profiles").?;
    // Fail-fast pin: the $meta map must still route this surface to its profile.
    const prof = getStr(getObj(getObj(r, "$meta").?, "surfaceProfiles").?, surface).?;
    if (!std.mem.eql(u8, prof, profile)) @panic("opRegistry.json: the cli is no longer a console-profile surface");
    const order = getArr(getObj(profiles, profile).?, "ops").?;
    // This surface's entries in the profile's (= prompt) order, minus entries restricted
    // to other surfaces.
    var entries: std.ArrayList(Entry) = .empty;
    for (order) |name| {
        for (getArr(r, "ops").?) |ev| {
            const e = ev.object;
            if (!strEq(e.get("name").?, name.string)) continue;
            if (!inList(.{ .string = profile }, e.get("profiles").?)) continue;
            if (e.get("surfaces")) |s| {
                if (!inList(.{ .string = surface }, s)) continue;
            }
            try entries.append(a, try resolveEntry(e));
        }
    }
    var forbidden: std.ArrayList([]const u8) = .empty;
    for (getArr(getObj(getObj(r, "forbidden").?, "perSurface").?, surface).?) |x| try forbidden.append(a, x.string);
    const ask = getObj(r, "ask").?;
    return .{
        .root = r,
        .limits = getObj(r, "limits").?,
        .regexes = getObj(r, "regexes").?,
        .entries = try entries.toOwnedSlice(a),
        .forbidden = try forbidden.toOwnedSlice(a),
        .ask = getObj(ask, "schema").?,
        .default_custom_label = getStr(ask, "defaultCustomLabel").?,
        .envelope = getObj(r, "envelope").?,
    };
}

/// The registry resolved for the cli (lazy; the first call parses the embedded JSON).
pub fn get() *const Schema {
    if (instance == null) instance = build() catch @panic("out of memory parsing opRegistry.json");
    return &instance.?;
}

const testing = std.testing;

test "grammars: the registry's token regexes, hand-matched" {
    try testing.expect(matches(.CROP_TOKEN, "10"));
    try testing.expect(matches(.CROP_TOKEN, "-10%"));
    try testing.expect(matches(.CROP_TOKEN, ".5in"));
    try testing.expect(matches(.CROP_TOKEN, "1.5cm"));
    try testing.expect(!matches(.CROP_TOKEN, ""));
    try testing.expect(!matches(.CROP_TOKEN, "-"));
    try testing.expect(!matches(.CROP_TOKEN, "1.")); // a dot needs a fraction
    try testing.expect(!matches(.CROP_TOKEN, "1..5"));
    try testing.expect(!matches(.CROP_TOKEN, "10 %"));
    try testing.expect(!matches(.CROP_TOKEN, "10pt"));
    try testing.expect(!matches(.CROP_TOKEN, "+10"));

    try testing.expect(matches(.CROP_ASPECT, "4:3") and matches(.CROP_ASPECT, "04:3"));
    inline for (.{ "0:3", "4:0", "-1:2", "3:4:5", "a:b", "1.5:2", "4", "4:", ":3", "1e2:3", "" }) |s|
        try testing.expect(!matches(.CROP_ASPECT, s));

    try testing.expect(matches(.PAGE_FORMAT, "a0") and matches(.PAGE_FORMAT, "b10"));
    try testing.expect(!matches(.PAGE_FORMAT, "a") and !matches(.PAGE_FORMAT, "a11") and !matches(.PAGE_FORMAT, "d4") and !matches(.PAGE_FORMAT, "A4"));

    try testing.expect(matches(.HEX, "#A1b2c3") and !matches(.HEX, "#0ff") and !matches(.HEX, "00ff00"));
    try testing.expect(matches(.CSS_NAME, "pink") and !matches(.CSS_NAME, "") and !matches(.CSS_NAME, "hot pink"));
    try testing.expect(matches(.FORMULA_X, "x*2 + (1)") and !matches(.FORMULA_X, "y*2") and !matches(.FORMULA_X, "") and !matches(.FORMULA_X, "x^2"));
    try testing.expect(matches(.FORMULA_Y, "y**2"));

    try testing.expect(matches(.HTTP_URL, "HTTP://b.example/x.jpg") and matches(.HTTP_URL, "https://a/b"));
    try testing.expect(!matches(.HTTP_URL, "https://") and !matches(.HTTP_URL, "https://a.example/a b") and !matches(.HTTP_URL, "ftp://a/x"));
    try testing.expect(!matches(.HTTP_URL, "https://a.example/a\u{00a0}b")); // JS \s is Unicode-aware

    try testing.expect(matches(.URL_SCHEME, "https://x") and matches(.URL_SCHEME, "file:///etc/passwd") and matches(.URL_SCHEME, "x+y.z-1://"));
    try testing.expect(!matches(.URL_SCHEME, "~/Downloads") and !matches(.URL_SCHEME, "C:\\x") and !matches(.URL_SCHEME, "1http://x") and !matches(.URL_SCHEME, "a:/b"));
}

test "schema: the cli's console entries, forbidden set and limits come from the registry" {
    const s = get();
    const expected = [_][]const u8{
        "crop",      "rotate", "filter",   "layout",  "formula", "page",   "blank",     "undo",
        "redo",      "reset",  "frame",    "image",   "save",    "accent", "connect",   "disconnect",
        "reconnect", "delete", "openFile", "openUrl", "copy",    "clear",  "clearChat",
    };
    try testing.expectEqual(expected.len, s.entries.len);
    for (expected, s.entries) |name, e| try testing.expectEqualStrings(name, e.name);
    // surfaceKeys.cli: crop's spec takes `album`; copy is field-less.
    try testing.expect(getObj(s.find("crop").?.keys, "spec").?.get("fields").?.object.get("album") != null);
    try testing.expectEqual(@as(usize, 0), s.find("copy").?.keys.count());
    // Flags merge, bullets resolve per surface/profile, the console addendum rides crop.
    try testing.expect(s.find("openFile").?.top_level_only and s.find("openFile").?.settings);
    try testing.expect(s.find("reconnect").?.settings and !s.find("reconnect").?.top_level_only);
    try testing.expect(s.find("clearChat").?.deferred);
    try testing.expect(s.find("redo").?.bullet == null);
    try testing.expect(std.mem.indexOf(u8, s.find("save").?.bullet.?, "~/Downloads") != null);
    try testing.expect(std.mem.indexOf(u8, s.find("crop").?.addendum.?, "\"album\": true") != null);
    try testing.expect(s.isForbidden("endpoint") and s.isForbidden("unshare") and !s.isForbidden("chat"));
    try testing.expectEqual(@as(f64, 16), s.limitNamed("MAX_ACTIONS"));
    try testing.expectEqual(@as(f64, 80), s.limitNamed("ask.label"));
    try testing.expectEqualStrings("Something else…", s.default_custom_label);
    try testing.expect(s.opsetEntry("extensionOpen", "rotate") == .entry);
    try testing.expect(s.opsetEntry("extensionOpen", "frame") == .fail);
    try testing.expect(s.opsetEntry("extensionOpen", "zoom") == .none);
}

test "numbers: 3.0 is an integer, overflowed literals are finite numbers, NaN-ish text is not" {
    try testing.expect(isInt(.{ .float = 3.0 }) and !isInt(.{ .float = 1.5 }));
    try testing.expect(numOf(.{ .number_string = "123456789012345678901234567890" }) != null);
    try testing.expect(numOf(.{ .number_string = "1e999" }) == null);
    try testing.expect(numOf(.{ .string = "3" }) == null);
}
