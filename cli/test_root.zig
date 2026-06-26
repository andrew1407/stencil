// Test entry point for `zig build test`. Rooted at cli/ so it may import both src/
// (the app modules + their inline unit tests) and tests/ (integration tests). The
// integration tests reach the app via "../src/*.zig" and load tests/fixtures/.
test {
    _ = @import("src/main.zig"); // inline unit tests (args, core, image, layout, ...)
    _ = @import("tests/format_roundtrip_test.zig");
    _ = @import("tests/decode_test.zig");
    _ = @import("tests/crop_test.zig");
    _ = @import("tests/rotate_test.zig");
    _ = @import("tests/layout_filter_test.zig");
    _ = @import("tests/pipeline_e2e_test.zig");
    _ = @import("tests/console_test.zig");
}
