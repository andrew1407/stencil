//! The report and project modes (CONTRACT.md §6): `--probe` measures an input, `--list-projects`
//! and `--project-info` read a server's project metadata, `--project-update` writes it, and
//! `--project-files` / `--project-file` list and download a project's stored files. Each prints one
//! JSON document on the stdout writer main.zig hands in and reports a failure as `error:` lines,
//! as `--merge-lines` does with the co-edit line union (§8).
const std = @import("std");
const args = @import("args.zig");
const probe = @import("inspect/probe.zig");
const projects = @import("inspect/projects.zig");
const update = @import("inspect/update.zig");
const files = @import("inspect/files.zig");
pub const mergeLines = @import("inspect/mergeLines.zig");

pub fn run(gpa: std.mem.Allocator, io: std.Io, out: *std.Io.Writer, opts: args.Options, kind: args.Inspect) !void {
    switch (kind) {
        .probe => try probe.run(gpa, io, out, opts),
        .list_projects => try projects.run(gpa, io, out, opts, null),
        .project_info => try projects.run(gpa, io, out, opts, opts.project_info.?),
        .project_update => try update.run(gpa, io, out, opts, opts.project_update.?),
        .project_files => try files.list(gpa, io, out, opts, opts.project_files.?),
        .project_file => try files.download(gpa, io, out, opts, opts.project_file.?),
    }
}

test {
    _ = probe;
    _ = projects;
    _ = update;
    _ = files;
    _ = mergeLines;
}
