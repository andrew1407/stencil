//! A server project's stored files, over `GET /projects/{id}/files/{kind}`: `--project-files <id>`
//! prints the project's public metadata and each kind it holds, told apart by its first bytes;
//! `--project-file <id> <kind>` writes one to <output> byte for byte, under the output guards.
const std = @import("std");
const args = @import("../args.zig");
const confine = @import("../safety/confine.zig");
const server = @import("../server/client.zig");
const report = @import("../app/report.zig");
const projects = @import("projects.zig");

/// How much of each file a listing asks for: past every magic `formatOf` reads.
const probe_bytes = 64;

pub fn list(gpa: std.mem.Allocator, io: std.Io, out: *std.Io.Writer, opts: args.Options, id: []const u8) !void {
    var client = try projects.connect(gpa, io, opts, "--project-files");
    defer client.deinit();
    var doc: std.Io.Writer.Allocating = .init(gpa);
    defer doc.deinit();
    var js: std.json.Stringify = .{ .writer = &doc.writer };
    listing(gpa, &client, &js, id) catch |e| {
        reportFailure(client.base, "file listing", id, e);
        return e;
    };
    try out.writeAll(doc.written());
    try out.writeByte('\n');
    try out.flush();
}

/// The project's public metadata plus `files`: `{kind, format}` for each kind it holds, in the
/// server's kind order. A kind the project lacks answers 404 and is left out.
pub fn listing(gpa: std.mem.Allocator, client: *server.Client, js: *std.json.Stringify, id: []const u8) !void {
    const record = try client.getProject(id);
    defer gpa.free(record);
    var parsed = std.json.parseFromSlice(std.json.Value, gpa, record, .{}) catch return server.Error.BadResponse;
    defer parsed.deinit();
    const project = if (parsed.value == .object) parsed.value.object.get("project") else null;
    if (project == null or project.? != .object) return server.Error.BadResponse;
    try js.beginObject();
    try projects.writeFields(js, project.?.object);
    try js.objectField("files");
    try js.beginArray();
    for (args.file_kinds) |kind| {
        const path = try std.fmt.allocPrint(gpa, "/projects/{s}/files/{s}", .{ id, kind });
        defer gpa.free(path);
        const range: std.http.Header = .{ .name = "range", .value = std.fmt.comptimePrint("bytes=0-{d}", .{probe_bytes - 1}) };
        const head = client.requestWith(.GET, path, null, null, range) catch |e| switch (e) {
            server.Error.NotFound => continue,
            else => return e,
        };
        defer gpa.free(head);
        try js.write(.{ .kind = kind, .format = formatOf(head) });
    }
    try js.endArray();
    try js.endObject();
}

pub fn download(gpa: std.mem.Allocator, io: std.Io, out: *std.Io.Writer, opts: args.Options, file: args.ProjectFile) !void {
    const path = opts.output orelse {
        report.err("--project-file writes the file to an output path — name one\n", .{});
        return error.NoOutput;
    };
    try guard(io, std.Io.Dir.cwd(), path, opts);
    var client = try projects.connect(gpa, io, opts, "--project-file");
    defer client.deinit();
    const bytes = client.downloadFile(file.id, file.kind) catch |e| {
        var what: [32]u8 = undefined;
        reportFailure(client.base, std.fmt.bufPrint(&what, "{s} file", .{file.kind}) catch file.kind, file.id, e);
        return e;
    };
    defer gpa.free(bytes);
    std.Io.Dir.cwd().writeFile(io, .{ .sub_path = path, .data = bytes }) catch |e| {
        report.err("could not write '{s}' ({s})\n", .{ path, @errorName(e) });
        return e;
    };
    var js: std.json.Stringify = .{ .writer = out };
    try js.write(.{ .id = file.id, .kind = file.kind, .path = path, .bytes = bytes.len, .format = formatOf(bytes) });
    try out.writeByte('\n');
    try out.flush();
}

/// The rules every CLI write keeps (CONTRACT.md §1), judged before anything is fetched: no `..`,
/// nothing outside the working directory under `--confine-output`, no existing file under `--no-clobber`.
pub fn guard(io: std.Io, dir: std.Io.Dir, path: []const u8, opts: args.Options) !void {
    if (confine.hasParentTraversal(path)) {
        report.err("refusing to write to a path that escapes the working directory: '{s}'\n", .{path});
        return error.UnsafeOutputPath;
    }
    if (opts.confine_output and confine.escapes(io, dir, path)) {
        report.err("--confine-output: refusing to write outside the working directory: '{s}'\n", .{path});
        return error.UnsafeOutputPath;
    }
    if (!opts.no_clobber) return;
    // A dangling link still names something the write would go through, as in refuseClobber.
    _ = dir.statFile(io, path, .{ .follow_symlinks = false }) catch return;
    report.err("--no-clobber: '{s}' already exists\n", .{path});
    return error.OutputExists;
}

/// What a file's first bytes say it is: an image by its magic, an mp4 or webm, a JSON document.
pub fn formatOf(b: []const u8) ?[]const u8 {
    const starts = struct {
        fn at(bytes: []const u8, off: usize, magic: []const u8) bool {
            return bytes.len >= off + magic.len and std.mem.eql(u8, bytes[off..][0..magic.len], magic);
        }
    }.at;
    if (starts(b, 0, "\x89PNG\r\n\x1a\n")) return "png";
    if (starts(b, 0, "\xff\xd8\xff")) return "jpg";
    if (starts(b, 0, "GIF8")) return "gif";
    if (starts(b, 0, "RIFF") and starts(b, 8, "WEBP")) return "webp";
    if (starts(b, 0, "BM")) return "bmp";
    if (starts(b, 4, "ftyp")) return "mp4";
    if (starts(b, 0, "\x1a\x45\xdf\xa3")) return "webm";
    const text = std.mem.trimStart(u8, b, " \t\r\n");
    if (text.len != 0 and (text[0] == '{' or text[0] == '[')) return "json";
    return null;
}

fn reportFailure(base: []const u8, what: []const u8, id: []const u8, e: anyerror) void {
    if (server.lastReject()) |r| return report.err("{s} refused the {s} of {s} ({d}): {s}\n", .{ base, what, id, r.status, r.message });
    switch (e) {
        error.BadResponse => report.err("{s} answered for {s} with something that is not a project\n", .{ base, id }),
        else => report.err("could not fetch the {s} of {s} from {s} ({s})\n", .{ what, id, base, @errorName(e) }),
    }
}

const testing = std.testing;

/// The project's GET, then per kind: an original PNG, a variant2 JPEG, a chat document, else 404.
const Store = struct {
    var ranges: usize = 0;

    fn run(gpa: std.mem.Allocator, _: std.Io, target: []const u8, _: std.http.Method, _: ?[]const u8, headers: []const std.http.Header) server.TransportError![]u8 {
        for (headers) |h| {
            if (std.mem.eql(u8, h.name, "range") and std.mem.eql(u8, h.value, "bytes=0-63")) ranges += 1;
        }
        if (std.mem.endsWith(u8, target, "/projects/p_1_a"))
            return gpa.dupe(u8, "{\"project\":{\"id\":\"p_1_a\",\"name\":\"Plans\",\"version\":3,\"originalPath\":\"p/o.png\"},\"layout\":{\"lines\":[]}}");
        if (std.mem.endsWith(u8, target, "/files/original")) return gpa.dupe(u8, "\x89PNG\r\n\x1a\n....");
        if (std.mem.endsWith(u8, target, "/files/variant2")) return gpa.dupe(u8, "\xff\xd8\xff\xe0");
        if (std.mem.endsWith(u8, target, "/files/chat")) return gpa.dupe(u8, " {\"messages\":[]}");
        return server.Error.NotFound;
    }
};

test "listing: the metadata, then each kind the project holds with its format; the paths stay out" {
    const a = testing.allocator;
    Store.ranges = 0;
    var c = server.Client{
        .gpa = a,
        .io = undefined, // the scripted transport never touches it
        .base = try a.dupe(u8, "http://s"),
        .token = try a.dupe(u8, "t"),
        .auth = try a.dupe(u8, "Bearer t"),
        .credential = try a.dupe(u8, ""),
        .transport = Store.run,
    };
    defer c.deinit();
    var out: std.Io.Writer.Allocating = .init(a);
    defer out.deinit();
    var js: std.json.Stringify = .{ .writer = &out.writer };
    try listing(a, &c, &js, "p_1_a");
    try testing.expectEqualStrings(
        \\{"id":"p_1_a","name":"Plans","version":3,"files":[{"kind":"original","format":"png"},{"kind":"chat","format":"json"},{"kind":"variant2","format":"jpg"}]}
    , out.written());
    try testing.expectEqual(args.file_kinds.len, Store.ranges);
}

test "formatOf: the magic of each kind a project stores, and null for anything else" {
    try testing.expectEqualStrings("webp", formatOf("RIFF\x00\x00\x00\x00WEBPVP8 ").?);
    try testing.expectEqualStrings("mp4", formatOf("\x00\x00\x00\x18ftypisom").?);
    try testing.expectEqualStrings("webm", formatOf("\x1a\x45\xdf\xa3\x01").?);
    try testing.expectEqualStrings("gif", formatOf("GIF89a").?);
    try testing.expect(formatOf("hello") == null);
    try testing.expect(formatOf("") == null);
}

test "guard: '..' always, a path outside the cwd under --confine-output, an existing file under --no-clobber" {
    const io = testing.io;
    var tmp = testing.tmpDir(.{});
    defer tmp.cleanup();
    try tmp.dir.writeFile(io, .{ .sub_path = "taken.png", .data = "x" });
    try testing.expectError(error.UnsafeOutputPath, guard(io, tmp.dir, "../out.png", .{}));
    try testing.expectError(error.UnsafeOutputPath, guard(io, tmp.dir, "/tmp/out.png", .{ .confine_output = true }));
    try guard(io, tmp.dir, "/tmp/out.png", .{});
    try testing.expectError(error.OutputExists, guard(io, tmp.dir, "taken.png", .{ .no_clobber = true }));
    try guard(io, tmp.dir, "taken.png", .{});
    try guard(io, tmp.dir, "free.png", .{ .no_clobber = true });
}
