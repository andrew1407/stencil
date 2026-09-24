//! The `/meow` cat face in the logo: it wanders the frame a cell at a time between random
//! spots, looks the way it walks, and at rest glances about or blinks. Every pose is a pure
//! function of the animation frame, so any repaint of a frame draws the same cat.
const std = @import("std");
const skin = @import("../skin.zig");

pub const Look = enum { open, blink, left, right, up, down };
pub const Face = struct { text: []const u8, cols: usize };

// One row per face style, one face per `Look`. ｀ is fullwidth, ˙ is a dot above.
const styles = [_][6]Face{
    .{ f("(=^･ｪ･^=)", 9), f("(=^-ｪ-^=)", 9), f("(=^･･ｪ^=)", 9), f("(=^ｪ･･^=)", 9), f("(=^˙ｪ˙^=)", 9), f("(=^.ｪ.^=)", 9) },
    .{ f("(=^･ω･^=)", 9), f("(=^-ω-^=)", 9), f("(=^･･ω^=)", 9), f("(=^ω･･^=)", 9), f("(=^˙ω˙^=)", 9), f("(=^.ω.^=)", 9) },
    .{ f("^._.^", 5), f("^-_-^", 5), f("^.._^", 5), f("^_..^", 5), f("^˙_˙^", 5), f("^,_,^", 5) },
    .{ f("(=｀ω´=)", 8), f("(=-ω-=)", 7), f("(=｀´ω=)", 8), f("(=ω｀´=)", 8), f("(=˙ω˙=)", 7), f("(=.ω.=)", 7) },
};

fn f(text: []const u8, cols: usize) Face {
    return .{ .text = text, .cols = cols };
}

// Each leg: walk to a new spot a cell per step, then rest there till the leg ends. A step is
// `step_frames` animation frames (80ms each).
const leg_frames = 24;
const step_frames = 2;

pub const Pose = struct { col: usize, row: usize, face: Face };

/// Where the cat is and how it looks this frame, inside a `width` × `rows` frame.
pub fn catPose(width: usize, rows: usize) Pose {
    const style = &styles[@intCast(skin.seedValue() % styles.len)];
    const span = width -| style[0].cols;
    const frame = skin.frame() / step_frames;
    const leg = frame / leg_frames;
    const t = frame % leg_frames;
    const from = spot(leg, span, rows);
    const to = spot(leg + 1, span, rows);
    const col = approach(from.col, to.col, t);
    const row = approach(from.row, to.row, t);
    const look: Look = if (col != to.col)
        (if (to.col > col) .right else .left)
    else if (row != to.row)
        (if (to.row > row) .down else .up)
    else
        rest(leg, t);
    const face = style[@intFromEnum(look)];
    return .{ .col = @min(col, width -| face.cols), .row = row, .face = face };
}

const Spot = struct { col: usize, row: usize };

fn spot(leg: u64, span: usize, rows: usize) Spot {
    const h = std.hash.Wyhash.hash(skin.seedValue(), std.mem.asBytes(&leg));
    return .{ .col = @intCast(h % (span + 1)), .row = @intCast((h >> 20) % @max(rows, 1)) };
}

// `steps` cells from `a` toward `b`, stopping on it.
fn approach(a: usize, b: usize, steps: usize) usize {
    return if (a < b) @min(b, a + steps) else b + ((a - b) -| steps);
}

// At rest: a blink just before setting off again, or a glance to one side on some legs.
fn rest(leg: u64, t: usize) Look {
    const h = std.hash.Wyhash.hash(skin.seedValue() +% 1, std.mem.asBytes(&leg));
    if (t + 3 >= leg_frames and t + 1 < leg_frames and h % 2 == 0) return .blink;
    if (h % 3 == 0 and t % 12 >= 6) return @enumFromInt(2 + (h >> 8) % 4);
    return .open;
}

const testing = std.testing;

test "cat: stays inside the frame and never moves more than a cell a frame" {
    skin.reseed(11);
    defer skin.reseed(0);
    var last = catPose(14, 6);
    var looks = [_]bool{false} ** 6;
    for (0..2000) |_| {
        skin.tick();
        const p = catPose(14, 6);
        try testing.expect(p.col + p.face.cols <= 14 and p.row < 6);
        const dc = if (p.col > last.col) p.col - last.col else last.col - p.col;
        const dr = if (p.row > last.row) p.row - last.row else last.row - p.row;
        try testing.expect(dc <= 2 and dr <= 1); // a narrower face may shift its clamp by one
        for (styles[@intCast(11 % styles.len)], 0..) |face, i| {
            if (std.mem.eql(u8, face.text, p.face.text)) looks[i] = true;
        }
        last = p;
    }
    for (looks) |seen| try testing.expect(seen); // open, blink and all four glances show up
}
