const std = @import("std");

// Source list for the shared C++ core (cli recompiles it directly rather than linking
// the CMake static library, so the CLI stays a self-contained `zig build`). KEEP IN
// SYNC with STENCIL_CORE_SOURCES in ../core/CMakeLists.txt.
const core_sources = [_][]const u8{
    "geometry/geometry.cpp",
    "geometry/cropGeometry.cpp",
    "geometry/imageOps.cpp",
    "geometry/rasterize.cpp",
    "color/color.cpp",
    "color/colorNames.cpp",
    "color/imageFilter.cpp",
    "parse/formulaParser.cpp",
    "parse/lengthTokens.cpp",
    "parse/cropSpec.cpp",
    "page/pageMetrics.cpp",
    "page/tooltipRows.cpp",
    "page/localeUnit.cpp",
    "page/hotkeyFormat.cpp",
    "state/historyStack.cpp",
    "state/projectsStore.cpp",
    "state/zoomPan.cpp",
    "state/holdDraw.cpp",
    "cliApi.cpp",
};

// Core group dirs (relative to ../core) put on the include path so the core's bare
// cross-group includes ("cropGeometry.hpp") resolve. Mirrors STENCIL_CORE_INCLUDE_DIRS.
const core_include_dirs = [_][]const u8{
    "../core",
    "../core/geometry",
    "../core/color",
    "../core/parse",
    "../core/page",
    "../core/state",
};

// Wire the C/C++ sources + include paths shared by the exe and test builds onto a
// module: the C++ core (codec-free) and the stb single-header image codecs.
fn wireNative(b: *std.Build, mod: *std.Build.Module, stb: *std.Build.Dependency) void {
    mod.link_libcpp = true; // C++ runtime for the core (formulaParser uses exceptions)
    // ".." resolves the C ABI header as "core/cliApi.h"; the core group dirs resolve the
    // core's bare cross-group includes; the stb dependency dir resolves "stb_*.h".
    mod.addIncludePath(b.path(".."));
    for (core_include_dirs) |dir| mod.addIncludePath(b.path(dir));
    mod.addIncludePath(stb.path("."));
    mod.addCSourceFiles(.{
        .root = b.path("../core"),
        .files = &core_sources,
        .flags = &.{"-std=c++17"},
    });
    mod.addCSourceFiles(.{
        .root = b.path("src"),
        .files = &.{"stb_impl.c"},
        // stb's JPEG encoder relies on signed-shift wraparound that is technically UB;
        // it's benign in C but Zig instruments C with UBSan in Debug and would trap.
        .flags = &.{ "-std=c11", "-fno-sanitize=undefined" },
    });
}

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    // stb_image / stb_image_write: public-domain single-header C codecs (build.zig.zon).
    const stb = b.dependency("stb", .{});

    const exe_mod = b.createModule(.{
        .root_source_file = b.path("src/main.zig"),
        .target = target,
        .optimize = optimize,
    });
    wireNative(b, exe_mod, stb);

    const exe = b.addExecutable(.{
        .name = "stencil",
        .root_module = exe_mod,
    });
    b.installArtifact(exe);

    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());
    if (b.args) |args| run_cmd.addArgs(args);
    const run_step = b.step("run", "Run the stencil CLI");
    run_step.dependOn(&run_cmd.step);

    // test_root.zig (at cli/ root) pulls in both the inline unit tests (via
    // src/main.zig) and the integration tests under tests/ (which use tests/fixtures/).
    // Rooting at cli/ lets the tests/ files import the src/ modules.
    const test_mod = b.createModule(.{
        .root_source_file = b.path("test_root.zig"),
        .target = target,
        .optimize = optimize,
    });
    wireNative(b, test_mod, stb);

    const unit_tests = b.addTest(.{ .root_module = test_mod });
    const run_tests = b.addRunArtifact(unit_tests);
    const test_step = b.step("test", "Run the stencil CLI unit + integration tests");
    test_step.dependOn(&run_tests.step);
}
