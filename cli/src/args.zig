//! Command-line flags. This file is the surface main.zig and the modes bind to; the option
//! blocks live in params/options.zig and the grammar in params/parse.zig.
const options = @import("params/options.zig");
const parser = @import("params/parse.zig");

pub const Blank = options.Blank;
pub const LayoutFrame = options.LayoutFrame;
pub const Options = options.Options;
pub const Mode = options.Mode;
pub const Error = options.Error;
pub const modeOf = options.modeOf;
pub const parse = parser.parse;
pub const parseBlank = parser.parseBlank;

test {
    _ = options;
    _ = parser;
}
