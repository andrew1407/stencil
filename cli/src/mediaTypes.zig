//! What the CLI counts as video, and how a format token is normalised — parsed from the
//! canonical shared JSON (browser/js/config/mediaTypes.json, embedded at build time), the one
//! asset the browser, extension and desktop read for the same questions. Follows theme.zig:
//! parsed lazily on first use (std.json needs an allocator comptime can't provide) into static
//! storage; the strings slice into the embedded JSON, and the CLI is single-threaded.
const std = @import("std");

const media_types_json = @embedFile("mediaTypes.json");

var loaded: bool = false;
var json_scratch: [32 * 1024]u8 = undefined;

var video_storage: [32][]const u8 = undefined;
var video_list: []const []const u8 = &.{};
var video_text: [32 * 8]u8 = undefined; // the extensions rewritten WITH their leading dot
var norm_storage: [8][2][]const u8 = undefined;
var norm_list: []const [2][]const u8 = &.{};
var ext_min: usize = 0;
var ext_max: usize = 0;
var image_prefix: []const u8 = "";
var video_prefix: []const u8 = "";

/// The video extensions THIS surface recognises — `surfaces.cli.video`, rewritten with the
/// leading dot looksLikeVideo matches on. Deliberately NOT `video.extensions`: no two
/// surfaces recognise the same set, and converging them is a behaviour change (asset `drift`).
pub fn videoExts() []const []const u8 {
    load();
    return video_list;
}

/// The substring rewrites applied to a lowercased format token, in the asset's order.
pub fn normalizations() []const [2][]const u8 {
    load();
    return norm_list;
}

/// Whether `len` is a plausible filename-extension length — the `{n,m}` bounds in the
/// asset's `extensionPattern`, which is all a surface without a regex engine needs of it.
pub fn extLenOk(len: usize) bool {
    load();
    return len >= ext_min and len <= ext_max;
}

/// The `image/` and `video/` media-type prefixes a `data:` URI is matched against.
pub fn imagePrefix() []const u8 {
    load();
    return image_prefix;
}
pub fn videoPrefix() []const u8 {
    load();
    return video_prefix;
}

fn load() void {
    if (loaded) return;
    loaded = true;
    var fba = std.heap.FixedBufferAllocator.init(&json_scratch);
    const root = std.json.parseFromSliceLeaky(std.json.Value, fba.allocator(), media_types_json, .{}) catch
        @panic("embedded mediaTypes.json is malformed");
    const obj = root.object;

    const rows = obj.get("surfaces").?.object.get("cli").?.object.get("video").?.array;
    if (rows.items.len == 0 or rows.items.len > video_storage.len) @panic("mediaTypes.json: bad cli video count");
    var n: usize = 0;
    for (rows.items, 0..) |v, i| {
        const ext = v.string;
        video_text[n] = '.';
        @memcpy(video_text[n + 1 ..][0..ext.len], ext);
        video_storage[i] = video_text[n .. n + 1 + ext.len];
        n += 1 + ext.len;
    }
    video_list = video_storage[0..rows.items.len];

    var it = obj.get("normalize").?.object.iterator(); // an ArrayHashMap — insertion order
    var k: usize = 0;
    while (it.next()) |kv| : (k += 1) {
        norm_storage[k] = .{ kv.key_ptr.*, kv.value_ptr.*.string };
    }
    norm_list = norm_storage[0..k];

    const pattern = obj.get("extensionPattern").?.string;
    const open = std.mem.indexOfScalar(u8, pattern, '{') orelse @panic("mediaTypes.json: extensionPattern has no {n,m}");
    const comma = std.mem.indexOfScalarPos(u8, pattern, open, ',').?;
    const close = std.mem.indexOfScalarPos(u8, pattern, comma, '}').?;
    ext_min = std.fmt.parseInt(usize, pattern[open + 1 .. comma], 10) catch @panic("mediaTypes.json: bad extensionPattern");
    ext_max = std.fmt.parseInt(usize, pattern[comma + 1 .. close], 10) catch @panic("mediaTypes.json: bad extensionPattern");

    image_prefix = obj.get("image").?.object.get("mimePrefix").?.string;
    video_prefix = obj.get("video").?.object.get("mimePrefix").?.string;
}
