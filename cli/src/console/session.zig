//! Console working-image state: the current RGBA8 image plus an undo/redo snapshot stack.
//! Each edit pushes a new state; `/undo`, `/redo` and `/reset` move a cursor over them. A
//! URL/blank/clipboard source is in-memory only and freed when replaced or on session end.
const std = @import("std");
const image = @import("../image.zig");

const max_states = 64; // original + up to 63 undoable edits; older edits drop off the front

pub const Session = struct {
    gpa: std.mem.Allocator,
    label: ?[]u8 = null, // owned display label (the source path / URL / "blank" / "clipboard")
    temp: bool = false, // in-memory only (URL, blank, clipboard), not backed by a file on disk
    default_fmt: image.Format = .png,
    states: std.ArrayList(image.Rgba8) = .empty, // [0] = original; the current image is states[cursor]
    cursor: usize = 0,

    pub fn deinit(self: *Session) void {
        self.clearAll();
        self.states.deinit(self.gpa);
    }

    pub fn hasImage(self: *Session) bool {
        return self.states.items.len != 0;
    }

    pub fn current(self: *Session) *image.Rgba8 {
        return &self.states.items[self.cursor];
    }

    /// A writable copy of the current image, for a transform to mutate before `commit`.
    pub fn workCopy(self: *Session) !image.Rgba8 {
        const cur = self.current();
        return .{ .width = cur.width, .height = cur.height, .pixels = try self.gpa.dupe(u8, cur.pixels) };
    }

    /// Replace the whole session with a freshly loaded source (its own fresh history).
    pub fn loadImage(self: *Session, img: image.Rgba8, label: []const u8, temp: bool, fmt: image.Format) !void {
        const dup = self.gpa.dupe(u8, label) catch |e| return freeImg(self.gpa, img, e);
        self.clearAll();
        self.states.append(self.gpa, img) catch |e| {
            self.gpa.free(dup);
            return freeImg(self.gpa, img, e);
        };
        self.label = dup;
        self.temp = temp;
        self.default_fmt = fmt;
        self.cursor = 0;
    }

    /// Make `work` the new current state, discarding any redo states ahead of the cursor.
    pub fn commit(self: *Session, work: image.Rgba8) !void {
        self.dropAfterCursor();
        self.states.append(self.gpa, work) catch |e| return freeImg(self.gpa, work, e);
        self.cursor = self.states.items.len - 1;
        // Bound memory: drop the oldest *edit* (never the original) once history is too deep.
        while (self.states.items.len > max_states) {
            self.states.items[1].deinit(self.gpa);
            _ = self.states.orderedRemove(1);
            self.cursor -= 1;
        }
    }

    pub fn undo(self: *Session) bool {
        if (self.cursor == 0) return false;
        self.cursor -= 1;
        return true;
    }

    pub fn redo(self: *Session) bool {
        if (self.cursor + 1 >= self.states.items.len) return false;
        self.cursor += 1;
        return true;
    }

    /// Revert to the original source, dropping every edit and the redo history.
    pub fn revert(self: *Session) void {
        self.cursor = 0;
        self.dropAfterCursor();
    }

    fn dropAfterCursor(self: *Session) void {
        var i = self.states.items.len;
        while (i > self.cursor + 1) : (i -= 1) self.states.items[i - 1].deinit(self.gpa);
        self.states.shrinkRetainingCapacity(self.cursor + 1);
    }

    pub fn clearAll(self: *Session) void {
        for (self.states.items) |*st| st.deinit(self.gpa);
        self.states.clearRetainingCapacity();
        self.cursor = 0;
        if (self.label) |l| self.gpa.free(l);
        self.label = null;
        self.temp = false;
        self.default_fmt = .png;
    }
};

fn freeImg(gpa: std.mem.Allocator, img: image.Rgba8, e: anyerror) anyerror {
    var m = img;
    m.deinit(gpa);
    return e;
}
