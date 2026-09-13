//! The .stc script modes. `check` reports diagnostics and `run` drives the edits; the core
//! owns the language, so everything here is about files, pixels and where output lands.
pub const load = @import("script/load.zig");
pub const check = @import("script/check.zig");
pub const sources = @import("script/sources.zig");
pub const save = @import("script/save.zig");
pub const apply = @import("script/apply.zig");
pub const run = @import("script/run.zig");

test {
    _ = load;
    _ = check;
    _ = sources;
    _ = save;
    _ = apply;
    _ = run;
}
