//! The logo art the secret skins draw in place of the S: the `/pieday` P, the `/bifrost` M
//! and the `/meow` cat face. `mark.zig` lays them into the frame; the wordmark each skin
//! shows beside it is a `skin.Traits` field.
const logo = @import("../logo.zig");
const skin = @import("../skin.zig");
const mark = @import("mark.zig");

const print = logo.print;

// The `/pieday` P in place of the S, at both sizes: the stem, then the bowl on the top rows.
pub const verts_p = [_]mark.Pt{
    .{ .col = 3, .row = 5 },  .{ .col = 3, .row = 0 },  .{ .col = 10, .row = 0 },
    .{ .col = 12, .row = 1 }, .{ .col = 10, .row = 2 }, .{ .col = 3, .row = 2 },
};
pub const verts_p_small = [_]mark.Pt{
    .{ .col = 2, .row = 4 }, .{ .col = 2, .row = 0 }, .{ .col = 7, .row = 0 },
    .{ .col = 8, .row = 1 }, .{ .col = 7, .row = 2 }, .{ .col = 2, .row = 2 },
};

// The `/bifrost` M: up the left stem, down into the middle, up again and down the right stem.
pub const verts_m = [_]mark.Pt{ .{ .col = 2, .row = 5 }, .{ .col = 2, .row = 0 }, .{ .col = 6, .row = 3 }, .{ .col = 7, .row = 3 }, .{ .col = 11, .row = 0 }, .{ .col = 11, .row = 5 } };
pub const verts_m_small = [_]mark.Pt{ .{ .col = 1, .row = 4 }, .{ .col = 1, .row = 0 }, .{ .col = 4, .row = 2 }, .{ .col = 5, .row = 2 }, .{ .col = 8, .row = 0 }, .{ .col = 8, .row = 4 } };

// The cat face in place of the S, on the row and column it has wandered to this frame.
pub fn catRow(row: usize, rows: usize, width: usize) void {
    const pose = skin.catPose(width, rows);
    if (row != pose.row or pose.face.cols > width) return mark.spaces(width);
    mark.spaces(pose.col);
    print("{s}", .{pose.face.text});
    mark.spaces(width - pose.face.cols - pose.col);
}
