//! Logo & output animations for the full-screen console: the logo button press, the
//! theme-change recolour wipe (seam + wordmark wave + the S icon's clock turn), and the
//! reveal sweep that types new output in from the left. All free functions over *Screen.
const press = @import("logoFx/press.zig");
const reveal = @import("logoFx/reveal.zig");
const clock = @import("logoFx/clock.zig");
const timing = @import("logoFx/timing.zig");

pub const pressLogo = press.pressLogo;
pub const wipeRecolor = press.wipeRecolor;
pub const revealing = reveal.revealing;
pub const revealNew = reveal.revealNew;

test {
    _ = press;
    _ = reveal;
    _ = clock;
    _ = timing;
    _ = @import("logoFx/rain.zig");
}
