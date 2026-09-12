//! The input history ring: what Up/Down recall. Trims, dedups consecutive entries, and caps
//! at max_history — it never reaches disk.
const std = @import("std");
const testing = std.testing;

pub const max_history = 50; // last-N entered commands kept for Up/Down

pub const History = struct {
    gpa: std.mem.Allocator,
    items: std.ArrayList([]u8) = .empty,

    pub fn deinit(self: *History) void {
        for (self.items.items) |it| self.gpa.free(it);
        self.items.deinit(self.gpa);
    }

    /// Record a command, ignoring blanks and consecutive duplicates; drops the oldest
    /// once `max_history` is exceeded.
    pub fn add(self: *History, line: []const u8) void {
        const t = std.mem.trim(u8, line, " \t\r\n");
        if (t.len == 0) return;
        const n = self.items.items.len;
        if (n > 0 and std.mem.eql(u8, self.items.items[n - 1], t)) return;
        const dup = self.gpa.dupe(u8, t) catch return;
        self.items.append(self.gpa, dup) catch {
            self.gpa.free(dup);
            return;
        };
        if (self.items.items.len > max_history) {
            self.gpa.free(self.items.items[0]);
            _ = self.items.orderedRemove(0);
        }
    }
};

test "History.add: trims, dedups consecutive, caps at max_history" {
    var h = History{ .gpa = testing.allocator };
    defer h.deinit();

    h.add("  /upload a.png  ");
    h.add("/upload a.png"); // consecutive duplicate — ignored
    h.add("   "); // blank — ignored
    h.add("/rotate 1");
    try testing.expectEqual(@as(usize, 2), h.items.items.len);
    try testing.expectEqualStrings("/upload a.png", h.items.items[0]);
    try testing.expectEqualStrings("/rotate 1", h.items.items[1]);

    var i: usize = 0;
    while (i < max_history + 10) : (i += 1) {
        var b: [16]u8 = undefined;
        h.add(std.fmt.bufPrint(&b, "/cmd {d}", .{i}) catch unreachable);
    }
    try testing.expectEqual(@as(usize, max_history), h.items.items.len);
}
