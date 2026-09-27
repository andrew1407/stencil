//! `--script <in.stc> --script-emit <out>`: the script re-written as a runnable script for
//! another surface. The OUTPUT's extension picks the target — `.js`/`.stcjs` javascript,
//! `.py`/`.pystc` python — so no flag names a language twice. Lengths, sources and save
//! targets are emitted AS WRITTEN; the generated file resolves them where it runs.
const std = @import("std");

const report = @import("../app/report.zig");
const scriptCore = @import("core.zig");

const common = @import("emit/common.zig");
const js = @import("emit/js.zig");
const py = @import("emit/py.zig");
const load = @import("load.zig");
const save_mod = @import("save.zig");

pub const Error = error{ UnknownEmitTarget, OutputExists } || common.Error;

/// What a backend returns when the target cannot carry a directive, with the span it refused.
pub const Refusal = common.Refusal;

pub const Target = enum {
    js,
    py,

    pub fn display(self: Target) []const u8 {
        return switch (self) {
            .js => js.display,
            .py => py.display,
        };
    }
};

const Extension = struct { ext: []const u8, target: Target };

/// The whole mapping: a suffix the user can write, and what it emits.
pub const EXTENSIONS = [_]Extension{
    .{ .ext = ".js", .target = .js },
    .{ .ext = ".stcjs", .target = .js },
    .{ .ext = ".py", .target = .py },
    .{ .ext = ".pystc", .target = .py },
};

/// The target `path` names, or null when its suffix is not one we emit.
pub fn targetFor(path: []const u8) ?Target {
    for (EXTENSIONS) |e| {
        if (path.len > e.ext.len and std.ascii.endsWithIgnoreCase(path, e.ext)) return e.target;
    }
    return null;
}

/// The emitted text, for a caller that owns where it goes. Returns the refusal's span when
/// the target cannot carry a directive — an unrunnable file is never written.
pub fn renderAlloc(
    gpa: std.mem.Allocator,
    script: scriptCore.Script,
    target: Target,
    label: []const u8,
    bad: *common.Refusal,
) ![]u8 {
    var out: std.Io.Writer.Allocating = .init(gpa);
    errdefer out.deinit();
    switch (target) {
        .js => try js.write(&out.writer, script, label, bad),
        .py => try py.write(&out.writer, script, label, bad),
    }
    return out.toOwnedSlice();
}

/// What may stop an emit before it reads the script: `--confine-output` and `--no-clobber`.
pub const Guards = struct { confine_output: bool = false, no_clobber: bool = false };

pub fn run(
    gpa: std.mem.Allocator,
    io: std.Io,
    in_path: []const u8,
    out_path: []const u8,
    confine_output: bool,
) !void {
    return runGuarded(gpa, io, in_path, out_path, .{ .confine_output = confine_output });
}

pub fn runGuarded(
    gpa: std.mem.Allocator,
    io: std.Io,
    in_path: []const u8,
    out_path: []const u8,
    guards: Guards,
) !void {
    const target = targetFor(out_path) orelse {
        report.err("cannot emit '{s}': name it .js, .stcjs, .py or .pystc\n", .{out_path});
        return Error.UnknownEmitTarget;
    };
    try save_mod.guard(io, out_path, guards.confine_output);
    // Checked as written (emit fills in no extension); a dangling link counts, as in refuseClobber.
    if (guards.no_clobber) {
        if (std.Io.Dir.cwd().statFile(io, out_path, .{ .follow_symlinks = false })) |_| {
            report.err("--no-clobber: '{s}' already exists\n", .{out_path});
            return Error.OutputExists;
        } else |_| {}
    }

    const source = try load.readScript(gpa, io, in_path);
    defer gpa.free(source);

    var script = scriptCore.Script.parse(source) catch return load.Error.ScriptUnreadable;
    defer script.deinit();

    const label = load.labelFor(in_path);
    if (load.reportDiagnostics(script, label)) return load.Error.ScriptHasErrors;

    var bad: common.Refusal = .{};
    const text = renderAlloc(gpa, script, target, label, &bad) catch |e| {
        if (e != Error.EmitUnsupported) return e;
        report.err("{s}:{d}:{d}: {s}\n", .{ label, bad.line, bad.col, bad.text() });
        return e;
    };
    defer gpa.free(text);

    std.Io.Dir.cwd().writeFile(io, .{ .sub_path = out_path, .data = text }) catch |e| {
        report.err("could not write {s} ({s})\n", .{ out_path, @errorName(e) });
        return e;
    };
    report.print("wrote {s} ({s})\n", .{ out_path, target.display() });
}

test {
    _ = common;
    _ = js;
    _ = py;
}
