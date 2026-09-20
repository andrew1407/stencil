//! The .stc script modes. `check` reports diagnostics, `plan` lowers to op-plan JSON,
//! `emit` re-writes the script for another surface and `run` drives the edits; the core owns the language, so everything here is about files,
//! pixels and where output lands.
pub const load = @import("script/load.zig");
pub const decode = @import("script/decode.zig");
pub const check = @import("script/check.zig");
pub const emit = @import("script/emit.zig");
pub const sources = @import("script/sources.zig");
pub const save = @import("script/save.zig");
pub const apply = @import("script/apply.zig");
pub const planActions = @import("script/planActions.zig");
pub const plan = @import("script/plan.zig");
pub const run = @import("script/run.zig");

test {
    _ = load;
    _ = decode;
    _ = check;
    _ = emit;
    _ = sources;
    _ = save;
    _ = apply;
    _ = planActions;
    _ = plan;
    _ = run;
}
