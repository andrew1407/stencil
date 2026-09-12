//! The §13 op registry. Table order IS prompt order: the core §2 ops first (their bullets
//! form §4's "Available ops" section), then the console profile (spliced in as the settings
//! block). One entry per `Action` variant, pinned at comptime below.
const std = @import("std");
const opplan = @import("../opplan.zig");
const descriptor = @import("descriptor.zig");
const coreOps = @import("coreOps.zig");
const consoleOps = @import("consoleOps.zig");

const Action = opplan.Action;
const OpDescriptor = descriptor.OpDescriptor;

pub const op_registry: [coreOps.ops.len + consoleOps.ops.len]OpDescriptor = coreOps.ops ++ consoleOps.ops;

// One entry per `Action` variant, each exactly once, and never a forbidden name.
comptime {
    @setEvalBranchQuota(100_000);
    const tags = @typeInfo(std.meta.Tag(Action)).@"enum".fields;
    if (op_registry.len != tags.len)
        @compileError("op_registry must carry exactly one entry per Action variant");
    for (tags) |t| {
        var hits: usize = 0;
        for (op_registry) |d| {
            if (std.mem.eql(u8, @tagName(d.tag), t.name)) hits += 1;
        }
        if (hits != 1) @compileError("op_registry must name Action." ++ t.name ++ " exactly once");
    }
}
