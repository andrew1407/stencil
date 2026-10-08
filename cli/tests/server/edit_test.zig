//! The live edit channel: frames are newline-delimited, only project events are read back, and
//! the connection's buffer reassembles a frame split across reads.
const std = @import("std");
const edit = @import("../../src/server/edit.zig");
const EditConn = edit.EditConn;
const frame = edit.frame;
const helloFrame = edit.helloFrame;
const parseEvent = edit.parseEvent;
const testing = std.testing;

test "helloFrame and frame are newline-delimited" {
    const a = testing.allocator;
    const h = try helloFrame(a, "tkn", "p_a_b", "c1");
    defer a.free(h);
    try testing.expect(h[h.len - 1] == '\n');
    try testing.expect(std.mem.indexOf(u8, h, "\"projectId\":\"p_a_b\"") != null);
    try testing.expect(std.mem.indexOf(u8, h, "\"token\":\"tkn\"") != null);

    const f = try frame(a, "{\"type\":\"ping\"}");
    defer a.free(f);
    try testing.expectEqualStrings("{\"type\":\"ping\"}\n", f);
}

test "parseEvent returns updated project-events, ignores other frames" {
    const a = testing.allocator;

    // A project-event frame yields an owned id/name/version + the change timestamp.
    const body = "{\"type\":\"project-event\",\"event\":\"updated\",\"project\":{\"id\":\"p_x_y\",\"name\":\"Notes\",\"version\":7,\"updatedAt\":1700000000000}}";
    var ev = (try parseEvent(a, body)).?;
    defer ev.deinit(a);
    try testing.expectEqualStrings("p_x_y", ev.id);
    try testing.expectEqualStrings("Notes", ev.name);
    try testing.expectEqual(@as(i64, 7), ev.version);
    try testing.expectEqual(@as(i64, 1700000000000), ev.updated_at);
    try testing.expect(!ev.deleted);

    // A "deleted" event is flagged; updatedAt absent defaults to 0.
    var del = (try parseEvent(a, "{\"type\":\"project-event\",\"event\":\"deleted\",\"project\":{\"id\":\"p_z\",\"name\":\"Gone\",\"version\":3}}")).?;
    defer del.deinit(a);
    try testing.expect(del.deleted);
    try testing.expectEqual(@as(i64, 0), del.updated_at);

    // Non-event frames (welcome, synced, hello echoes) are ignored.
    try testing.expect((try parseEvent(a, "{\"type\":\"welcome\",\"version\":1}")) == null);
    try testing.expect((try parseEvent(a, "{\"type\":\"project-event\"}")) == null); // no project
    try testing.expect((try parseEvent(a, "not json")) == null);
}

test "EditConn frame buffer handles partial, multiple, and skipped frames" {
    const a = testing.allocator;
    // io/stream are unused by feed/nextEvent (pure buffer work), so leave them undefined.
    var c = EditConn{ .gpa = a, .io = undefined, .stream = undefined };
    defer c.rbuf.deinit(a);

    // A frame split across two reads yields nothing until the newline arrives.
    try c.feed("{\"type\":\"project-event\",\"event\":\"updated\",\"project\":{\"id\":\"p1\",\"nam");
    try testing.expect((try c.nextEvent()) == null);

    // Completing it, plus a non-event frame and a second event, all in one chunk.
    try c.feed("e\":\"A\",\"version\":2}}\n{\"type\":\"welcome\",\"version\":1}\n" ++
        "{\"type\":\"project-event\",\"event\":\"updated\",\"project\":{\"id\":\"p2\",\"name\":\"B\",\"version\":5}}\n");

    var e1 = (try c.nextEvent()).?;
    defer e1.deinit(a);
    try testing.expectEqualStrings("p1", e1.id);
    try testing.expectEqual(@as(i64, 2), e1.version);

    // The welcome frame is skipped; the next event is p2.
    var e2 = (try c.nextEvent()).?;
    defer e2.deinit(a);
    try testing.expectEqualStrings("p2", e2.id);
    try testing.expectEqual(@as(i64, 5), e2.version);

    // Buffer drained.
    try testing.expect((try c.nextEvent()) == null);
}

test "a frame past the server's 16 MiB cap closes the feed instead of growing the buffer" {
    const a = testing.allocator;
    var c = EditConn{ .gpa = a, .io = undefined, .stream = undefined };
    defer c.rbuf.deinit(a);
    const big = try a.alloc(u8, edit.max_frame_bytes);
    defer a.free(big);
    @memset(big, 'x');

    // Exactly the cap is still a frame; its newline completes it.
    try c.feed(big[0 .. big.len - 1]);
    try c.feed("x\n");
    try testing.expect((try c.nextEvent()) == null);
    try testing.expectEqual(@as(usize, 0), c.rbuf.items.len);

    // One byte more, split over reads, and the subscription is dropped with nothing held.
    try c.feed(big[0..4096]);
    try testing.expectError(error.FrameTooLarge, c.feed(big));
    try testing.expect(c.closed);
    try testing.expectEqual(@as(usize, 0), c.rbuf.capacity);
    try testing.expect((try c.nextEvent()) == null);
}

test "nextEvent pops a long run of frames in order without re-copying the buffer per frame" {
    const a = testing.allocator;
    var c = EditConn{ .gpa = a, .io = undefined, .stream = undefined };
    defer c.rbuf.deinit(a);
    var chunk: std.ArrayList(u8) = .empty;
    defer chunk.deinit(a);
    for (0..300) |i| try chunk.print(a, "{{\"type\":\"project-event\",\"project\":{{\"id\":\"p{d}\",\"version\":{d}}}}}\n", .{ i, i });
    try c.feed(chunk.items);
    for (0..300) |i| {
        var ev = (try c.nextEvent()).?;
        defer ev.deinit(a);
        try testing.expectEqual(@as(i64, @intCast(i)), ev.version);
        if (i < 299) try testing.expect(c.head > 0); // consumed in place, compacted on the next feed
    }
    try testing.expect((try c.nextEvent()) == null);
    try testing.expectEqual(@as(usize, 0), c.head);
}
