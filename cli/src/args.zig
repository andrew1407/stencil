//! Command-line flags. This file is the surface main.zig and the modes bind to; the option
//! blocks live in params/options.zig and the grammar in params/parse.zig.
const options = @import("params/options.zig");
const parser = @import("params/parse.zig");
const inspect_flags = @import("params/inspect.zig");

pub const Blank = options.Blank;
pub const LayoutFrame = options.LayoutFrame;
pub const Options = options.Options;
pub const Mode = options.Mode;
pub const Inspect = options.Inspect;
pub const EnvTokens = options.EnvTokens;
pub const ProjectSet = options.ProjectSet;
pub const ProjectFile = options.ProjectFile;
pub const file_kinds = inspect_flags.file_kinds;
pub const Error = options.Error;
pub const modeOf = options.modeOf;
pub const parse = parser.parse;

test {
    _ = options;
    _ = parser;
}
