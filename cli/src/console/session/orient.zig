//! The orientation edits — a quarter turn and a left-right flip of the shown view. Each pushes one
//! snapshot; the crop and the lines ride along as the browser's rotate and flip carry them.
const Session = @import("../session.zig").Session;
const core = @import("../../core.zig");
const rotateCropQuarters = @import("../session.zig").rotateCropQuarters;
const layoutJson = @import("layoutJson.zig");

/// Rotate by `n` quarter-turns (clockwise). The crop and the lines ride along into the new space,
/// the lines turning inside the pre-turn view as the browser's rotate turns them.
pub fn applyRotate(self: *Session, n: i32) !void {
    const cur = self.state();
    var next = try cur.dupe(self.gpa);
    errdefer next.deinit(self.gpa);
    const orig = self.original.?;
    const view = if (cur.crop) |cr| core.Size{ .w = cr.w, .h = cr.h } else core.rotatedDims(@intCast(orig.width), @intCast(orig.height), cur.rotation);
    if (next.crop) |cr| next.crop = rotateCropQuarters(cr, cur.rotation, @intCast(orig.width), @intCast(orig.height), n).crop;
    if (next.lines_json.len != 0) {
        const turned = try layoutJson.turnLinesJson(self.gpa, next.lines_json, n, view.w, view.h);
        self.gpa.free(next.lines_json);
        next.lines_json = turned;
    }
    next.rotation = core.normalizeQuarters(cur.rotation + n);
    try self.pushState(next);
}

/// Mirror the shown view left-right: the crop reflects across the turned width, the turn negates and
/// the lines mirror inside the view, as the browser's flip does them.
pub fn applyFlip(self: *Session) !void {
    const cur = self.state();
    var next = try cur.dupe(self.gpa);
    errdefer next.deinit(self.gpa);
    const orig = self.original.?;
    const ow: i32 = @intCast(orig.width);
    const oh: i32 = @intCast(orig.height);
    const view_w = if (cur.crop) |cr| cr.w else core.rotatedDims(ow, oh, cur.rotation).w;
    if (cur.crop) |cr| next.crop = core.mirrorEdit(cr, cur.rotation, ow, oh).crop;
    if (next.lines_json.len != 0) {
        const flipped = try layoutJson.mirrorLinesJson(self.gpa, next.lines_json, view_w);
        self.gpa.free(next.lines_json);
        next.lines_json = flipped;
    }
    next.rotation = core.normalizeQuarters(-cur.rotation);
    next.mirrored = !cur.mirrored;
    try self.pushState(next);
}
