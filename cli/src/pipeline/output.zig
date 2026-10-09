//! The pipeline's last step: the one place a result reaches the disk, and the refusals checked
//! before it — `..` always, `--confine-output` and `--no-clobber` when asked — each on the name
//! the output really lands under, a missing extension filled from the input's format.
const std = @import("std");
const image = @import("../media/image.zig");
const report = @import("../app/report.zig");
const confine = @import("../safety/confine.zig");
const sources = @import("sources.zig");

const expandHome = sources.expandHome;
const hasParentTraversal = confine.hasParentTraversal;

/// Encode the image, write it to `out` (extension filled from `default_fmt` if absent), and print the
/// canonical `wrote {path} ({w}x{h} px · {page})` line with the given page label.
pub fn writeOutputLabeled(gpa: std.mem.Allocator, io: std.Io, img: image.Rgba8, out: []const u8, default_fmt: image.Format, page_label: []const u8) !void {
    const dir = std.Io.Dir.cwd();
    const resolved = try resolveOutput(gpa, out, default_fmt);
    defer gpa.free(resolved.path);
    try refuseDirectory(io, resolved.path);

    const encoded = try image.encode(gpa, img, resolved.fmt);
    defer gpa.free(encoded);
    // The only place a result reaches the disk, so it owns the failure line too: a caller
    // that just propagated would exit 1 with nothing said, and adapters parse `error:`.
    dir.writeFile(io, .{ .sub_path = resolved.path, .data = encoded }) catch |e| {
        report.err("could not write {s} ({s})\n", .{ resolved.path, @errorName(e) });
        return e;
    };

    report.print("wrote {s} ({d}x{d} px · {s})\n", .{ resolved.path, img.width, img.height, page_label });
}

/// `--confine-output` on the file `out` lands at (extension filled from `default_fmt`): outside the
/// working directory by its spelling or through a symbolic link.
pub fn refuseEscape(gpa: std.mem.Allocator, io: std.Io, out: []const u8, default_fmt: image.Format) !void {
    const resolved = try resolveOutput(gpa, out, default_fmt);
    defer gpa.free(resolved.path);
    if (!confine.escapes(io, std.Io.Dir.cwd(), resolved.path)) return;
    report.err("--confine-output: refusing to write outside the working directory: '{s}'\n", .{out});
    return error.UnsafeOutputPath;
}

/// `--no-clobber`: refuse when the file `out` would land at (extension filled from `default_fmt`,
/// or `out` itself when `as_is`) already exists. Called before the work, so a refusal fetches and uploads nothing.
pub fn refuseClobber(gpa: std.mem.Allocator, io: std.Io, out: []const u8, default_fmt: image.Format, as_is: bool) !void {
    const path = if (as_is) try expandHome(gpa, out) else (try resolveOutput(gpa, out, default_fmt)).path;
    defer gpa.free(path);
    // A dangling link still names something the write would go through, so links are not followed.
    // Any other stat failure is left for the write itself to report.
    _ = std.Io.Dir.cwd().statFile(io, path, .{ .follow_symlinks = false }) catch return;
    report.err("--no-clobber: '{s}' already exists\n", .{path});
    return error.OutputExists;
}

/// An existing directory under the name the output lands at (`shots.png/`) is refused before the
/// encode, with the same line an empty or `.` basename gets.
fn refuseDirectory(io: std.Io, path: []const u8) !void {
    const st = std.Io.Dir.cwd().statFile(io, path, .{}) catch return;
    if (st.kind != .directory) return;
    report.err("the output '{s}' names a directory, not a file — give the result a file name\n", .{path});
    return error.NoOutputName;
}

const Resolved = struct { path: []u8, fmt: image.Format };

// A recognised extension selects the format; otherwise fall back to `fallback` and
// append its extension (so `out` becomes `out.png`, `result` -> `result.jpg`, etc.).
fn resolveOutput(gpa: std.mem.Allocator, out_raw: []const u8, fallback: image.Format) !Resolved {
    // A typed "~/Downloads/x.png" means the home directory, not one named "~".
    const out = try expandHome(gpa, out_raw);
    errdefer gpa.free(out);
    // Refuse an output path that climbs above the working directory. Direct users still write anywhere they
    // name; this only blocks the ".." traversal an adapter forwarding an untrusted name should not do.
    if (hasParentTraversal(out)) {
        report.err("refusing to write to a path that escapes the working directory: '{s}'\n", .{out});
        return error.UnsafeOutputPath; // the errdefer above frees `out`
    }
    const base = out[if (std.mem.lastIndexOfAny(u8, out, "/\\")) |sep| sep + 1 else 0..];
    if (base.len == 0 or std.mem.eql(u8, base, ".") or std.mem.eql(u8, base, "..")) {
        report.err("the output '{s}' names a directory, not a file — give the result a file name\n", .{out});
        return error.NoOutputName;
    }
    if (image.extOf(out)) |e| {
        if (image.formatFromExt(e)) |f| return .{ .path = out, .fmt = f };
    }
    defer gpa.free(out);
    const path = try std.fmt.allocPrint(gpa, "{s}.{s}", .{ out, fallback.ext() });
    return .{ .path = path, .fmt = fallback };
}

const testing = std.testing;

test "refuseClobber checks the name the output lands under, extension filled" {
    const gpa = testing.allocator;
    var threaded = std.Io.Threaded.init(gpa, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();
    try dir.writeFile(io, .{ .sub_path = "stencil_clobber_probe.jpg", .data = "x" });
    defer dir.deleteFile(io, "stencil_clobber_probe.jpg") catch {};

    try testing.expectError(error.OutputExists, refuseClobber(gpa, io, "stencil_clobber_probe", .jpeg, false));
    try testing.expectError(error.OutputExists, refuseClobber(gpa, io, "stencil_clobber_probe.jpg", .png, false));
    try refuseClobber(gpa, io, "stencil_clobber_probe", .png, false); // lands as .png: free
    try refuseClobber(gpa, io, "stencil_clobber_probe", .jpeg, true); // a project name is taken as-is
}

test "resolveOutput rejects parent-directory traversal" {
    const gpa = testing.allocator;
    try testing.expectError(error.UnsafeOutputPath, resolveOutput(gpa, "../../etc/evil.png", image.Format.png));
    try testing.expectError(error.UnsafeOutputPath, resolveOutput(gpa, "sub/../../out.png", image.Format.png));
    for ([_][]const u8{ ".", "sub/", "sub/.", "" }) |dir| try testing.expectError(error.NoOutputName, resolveOutput(gpa, dir, image.Format.png));
    // A normal relative name is accepted and keeps its extension.
    const ok = try resolveOutput(gpa, "out.png", image.Format.png);
    defer gpa.free(ok.path);
    try testing.expectEqualStrings("out.png", ok.path);
}

test "an existing directory under the output's name is refused before the write" {
    const gpa = testing.allocator;
    var threaded = std.Io.Threaded.init(gpa, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();
    try dir.createDirPath(io, "stencil_dir_probe.png");
    defer dir.deleteDir(io, "stencil_dir_probe.png") catch {};

    try testing.expectError(error.NoOutputName, refuseDirectory(io, "stencil_dir_probe.png"));
    try refuseDirectory(io, "stencil_dir_probe_absent.png");
}
