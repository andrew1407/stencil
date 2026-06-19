// Every output format encodes and decodes back at the right dimensions.
const std = @import("std");
const image = @import("../src/image.zig");
const testing = std.testing;

test "each output format round-trips dimensions" {
    const a = testing.allocator;
    var px = [_]u8{
        10, 20,  30,  255, 40,  50,  60,  255, 70,  80,  90,  255,
        130, 140, 150, 255, 160, 170, 180, 255, 190, 200, 210, 255,
    };
    const img = image.Rgba8{ .width = 3, .height = 2, .pixels = &px };
    inline for (.{ image.Format.png, image.Format.jpeg, image.Format.bmp, image.Format.tga }) |fmt| {
        const enc = try image.encode(a, img, fmt);
        defer a.free(enc);
        try testing.expect(enc.len > 0);
        var dec = try image.decode(a, enc);
        defer dec.deinit(a);
        try testing.expectEqual(@as(usize, 3), dec.width);
        try testing.expectEqual(@as(usize, 2), dec.height);
    }
}
