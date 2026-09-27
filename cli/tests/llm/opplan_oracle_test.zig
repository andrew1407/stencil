// The typed op-plan oracle: llm.parsePlan's whole result — verdict, reply, warnings, error and the
// typed actions / variants / ask card — over every hand-written, generated and adversarial case,
// pinned in tests/pins/opplan_oracle.json. STENCIL_UPDATE_ORACLE=1 re-records it. Twin of the
// desktop's opPlanOracle.headless.cpp and pystencil's test_llm_plan_oracle.py.
const std = @import("std");
const llm = @import("../../src/llm.zig");
const fx = @import("../fixture_corpus.zig");
const testing = std.testing;

const golden = "tests/pins/opplan_oracle.json"; // `zig build test` runs with cwd = cli/
const dir = "llm/fixtures/opPlan/";
// A string past this many bytes is pinned by its length and digest.
const long = 160;

/// A reply as a model sends it: verbatim, object-serialised, chunks repeated, or raw bytes.
fn caseText(a: std.mem.Allocator, c: std.json.Value) ![]const u8 {
    if (fx.memberStr(c, "inputBase64")) |b64| {
        const out = try a.alloc(u8, try std.base64.standard.Decoder.calcSizeForSlice(b64));
        try std.base64.standard.Decoder.decode(out, b64);
        return out;
    }
    if (fx.member(c, "parts")) |parts| {
        var out: std.ArrayList(u8) = .empty;
        for (parts.array.items) |p| {
            for (0..@intCast(p.array.items[1].integer)) |_| try out.appendSlice(a, p.array.items[0].string);
        }
        return out.items;
    }
    const input = fx.member(c, "input").?;
    return if (input == .string) input.string else fx.stringify(a, input);
}

fn str(js: *std.json.Stringify, s: []const u8) !void {
    if (s.len <= long) return js.write(s);
    var digest: [32]u8 = undefined;
    std.crypto.hash.sha2.Sha256.hash(s, &digest, .{});
    var buf: [64]u8 = undefined;
    try js.write(try std.fmt.bufPrint(&buf, "<{d} bytes sha256:{x}>", .{ s.len, digest[0..8] }));
}

fn value(js: *std.json.Stringify, v: std.json.Value) !void {
    switch (v) {
        .string => |s| try str(js, s),
        .array => |arr| {
            try js.beginArray();
            for (arr.items) |x| try value(js, x);
            try js.endArray();
        },
        .object => |obj| {
            try js.beginObject();
            var it = obj.iterator();
            while (it.next()) |kv| {
                try js.objectField(kv.key_ptr.*);
                try value(js, kv.value_ptr.*);
            }
            try js.endObject();
        },
        else => try js.write(v),
    }
}

/// Any typed field: strings pinned, structs as objects, slices as arrays, the rest as std.json writes it.
fn any(js: *std.json.Stringify, a: std.mem.Allocator, name: []const u8, v: anytype) !void {
    const T = @TypeOf(v);
    if (T == []const u8 or T == []u8) {
        if (std.mem.eql(u8, name, "lines_json")) return value(js, try std.json.parseFromSliceLeaky(std.json.Value, a, v, .{}));
        return str(js, v);
    }
    if (T == u8 and std.mem.eql(u8, name, "axis")) return js.write(if (v == 0) "" else &[_]u8{v});
    switch (@typeInfo(T)) {
        .optional => return if (v) |x| any(js, a, name, x) else js.write(null),
        .@"struct" => |s| {
            try js.beginObject();
            inline for (s.fields) |f| {
                try js.objectField(f.name);
                try any(js, a, f.name, @field(v, f.name));
            }
            return js.endObject();
        },
        .pointer => {
            try js.beginArray();
            for (v) |x| try any(js, a, name, x);
            return js.endArray();
        },
        .@"union" => {
            try js.beginObject();
            try js.objectField("op");
            try js.write(@tagName(v));
            switch (v) {
                inline else => |payload| if (@typeInfo(@TypeOf(payload)) == .@"struct") {
                    inline for (@typeInfo(@TypeOf(payload)).@"struct".fields) |f| {
                        try js.objectField(comptime if (std.mem.eql(u8, f.name, "lines_json")) "lines" else f.name);
                        try any(js, a, f.name, @field(payload, f.name));
                    }
                },
            }
            return js.endObject();
        },
        else => return js.write(v),
    }
}

fn typedResult(js: *std.json.Stringify, a: std.mem.Allocator, text: []const u8) !void {
    try js.beginObject();
    switch (try llm.parsePlan(testing.allocator, text)) {
        .invalid => |msg| {
            defer testing.allocator.free(msg);
            try js.objectField("verdict");
            try js.write("invalid");
            try js.objectField("error");
            try str(js, msg);
        },
        .plan => |p| {
            var plan = p;
            defer plan.deinit();
            try js.objectField("verdict");
            try js.write(if (plan.chat_only) "chatOnly" else "valid");
            inline for (.{ "reply", "warnings", "actions", "variants", "ask" }) |f| {
                try js.objectField(f);
                try any(js, a, f, @field(plan, f));
            }
        },
    }
    try js.endObject();
}

test "op-plan oracle: every corpus case's typed result matches tests/pins/opplan_oracle.json" {
    var w = fx.Walk.start();
    defer w.stop();
    const a = w.alloc();
    var out: std.Io.Writer.Allocating = .init(a);
    try out.writer.writeAll("{");
    var cases: usize = 0;
    inline for (.{ "cases.json", "generated/cases.json", "oracle/inputs.json" }) |bundle| {
        for (fx.member(try fx.loadJson(a, w.io(), dir ++ bundle), "cases").?.array.items) |c| {
            const name = fx.memberStr(c, "name").?;
            const label = fx.memberStr(c, "file") orelse if (std.mem.startsWith(u8, bundle, "generated")) try std.fmt.allocPrint(a, "{s}.json", .{name}) else name;
            try out.writer.writeAll(if (cases == 0) "\n" else ",\n");
            var js: std.json.Stringify = .{ .writer = &out.writer };
            try js.write(label);
            try out.writer.writeAll(": ");
            js = .{ .writer = &out.writer };
            try typedResult(&js, a, try caseText(a, c));
            cases += 1;
        }
    }
    try out.writer.writeAll("\n}\n");
    try testing.expect(cases >= 690);

    if (std.c.getenv("STENCIL_UPDATE_ORACLE")) |v| if (std.mem.eql(u8, std.mem.span(v), "1")) {
        return std.Io.Dir.cwd().writeFile(w.io(), .{ .sub_path = golden, .data = out.written() });
    };
    const want = std.Io.Dir.cwd().readFileAlloc(w.io(), golden, a, .limited(16 << 20)) catch |e| {
        std.debug.print("oracle '{s}' unreadable ({s}) — record it with STENCIL_UPDATE_ORACLE=1 zig build test\n", .{ golden, @errorName(e) });
        return e;
    };
    var got_lines = std.mem.splitScalar(u8, out.written(), '\n');
    var want_lines = std.mem.splitScalar(u8, want, '\n');
    while (got_lines.next()) |g| {
        const x = want_lines.next() orelse "";
        if (!std.mem.eql(u8, g, x)) w.fail("[DIFF]\n   got: {s}\n  want: {s}\n", .{ g, x });
    }
    if (want_lines.next()) |extra| w.fail("[DIFF] the oracle holds more cases, from: {s}\n", .{extra});
    w.walked = cases;
    try w.report("opPlan oracle");
}
