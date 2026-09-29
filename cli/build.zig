const std = @import("std");

// Source list for the shared C++ core — the CLI recompiles it rather than linking core's CMake
// static library. KEEP IN SYNC with STENCIL_CORE_SOURCES in ../core/CMakeLists.txt.
const core_sources = [_][]const u8{
    "geometry/pointMath.cpp",
    "geometry/hitTest.cpp",
    "geometry/cropGeometry.cpp",
    "geometry/cropSnap.cpp",
    "geometry/lineChain.cpp",
    "color/color.cpp",
    "color/colorNames.cpp",
    "raster/imageOps.cpp",
    "raster/rasterize.cpp",
    "raster/strokeCoverage.cpp",
    "raster/markers.cpp",
    "raster/imageFilter.cpp",
    "raster/downscale.cpp",
    "parse/formulaContext.cpp",
    "parse/formulaParser.cpp",
    "parse/DurationParser.cpp",
    "parse/lengthTokens.cpp",
    "parse/cropSpec.cpp",
    "page/pageMetrics.cpp",
    "page/localeUnit.cpp",
    "format/tooltipRows.cpp",
    "state/HistoryStack.cpp",
    "state/lineMerge.cpp",
    "state/ProjectsStore.cpp",
    "state/zoomPan.cpp",
    "state/holdDraw.cpp",
    "script/diagnostics.cpp",
    "script/lexer.cpp",
    "script/parser.cpp",
    "script/values.cpp",
    "script/args.cpp",
    "script/crop.cpp",
    "script/lineStyle.cpp",
    "script/templates.cpp",
    "script/undo.cpp",
    "script/program/scriptHistory.cpp",
    "script/lower.cpp",
    "script/program/scriptProgram.cpp",
    "script/dump.cpp",
    "json/jsText.cpp",
    "json/jsNumber.cpp",
    "json/jsonValue.cpp",
    "json/jsonScan.cpp",
    "json/jsonReader.cpp",
    "json/jsonWriter.cpp",
    "opplan/planGrammars.cpp",
    "opplan/planPath.cpp",
    "opplan/planSchema.cpp",
    "opplan/planSchemaCheck.cpp",
    "opplan/planChecks.cpp",
    "opplan/planFields.cpp",
    "opplan/planRules.cpp",
    "opplan/planResult.cpp",
    "opplan/planWalker.cpp",
    "opplan/planWalkerAsk.cpp",
    "opplan/planWalk.cpp",
    "cliApi.cpp",
};

// Core group dirs (relative to ../core) put on the include path so the core's bare
// cross-group includes ("cropGeometry.hpp") resolve. Mirrors STENCIL_CORE_INCLUDE_DIRS.
const core_include_dirs = [_][]const u8{
    "../core",
    "../core/abi",
    "../core/script",
    "../core/script/program",
    "../core/geometry",
    "../core/raster",
    "../core/color",
    "../core/parse",
    "../core/page",
    "../core/format",
    "../core/state",
    "../core/json",
    "../core/opplan",
};

// Wire the C/C++ sources + include paths shared by the exe and test builds onto a
// module: the C++ core (codec-free) and the stb single-header image codecs.
fn wireNative(b: *std.Build, mod: *std.Build.Module, stb: *std.Build.Dependency) void {
    mod.link_libcpp = true; // the core's STL (containers, strings) needs the C++ runtime
    // ".." resolves the C ABI header as "core/cliApi.h"; the core group dirs resolve the
    // core's bare cross-group includes; the stb dependency dir resolves "stb_*.h".
    mod.addIncludePath(b.path(".."));
    for (core_include_dirs) |dir| mod.addIncludePath(b.path(dir));
    mod.addIncludePath(stb.path("."));
    // ReleaseSmall shrinks the Zig code only: C/C++ keep ReleaseFast's -O2 (the later flag wins
    // over Zig's -Os), so the core and codec objects stay byte-identical to a ReleaseFast build.
    const small = mod.optimize == .ReleaseSmall;
    mod.addCSourceFiles(.{
        .root = b.path("../core"),
        .files = &core_sources,
        .flags = if (small) &.{ "-std=c++17", "-O2" } else &.{"-std=c++17"},
    });
    // Canonical shared static data from the browser app, embeddable via
    // @embedFile("<name>") (cross-tree paths need an anonymous import).
    mod.addAnonymousImport("accents.json", .{ .root_source_file = b.path("../browser/js/config/accents.json") });
    mod.addAnonymousImport("colorNames.json", .{ .root_source_file = b.path("../browser/js/config/colorNames.json") });
    mod.addAnonymousImport("blockedRanges.json", .{ .root_source_file = b.path("../browser/js/config/net/blockedRanges.json") });
    mod.addAnonymousImport("constants.json", .{ .root_source_file = b.path("../browser/js/config/constants.json") });
    mod.addAnonymousImport("mediaTypes.json", .{ .root_source_file = b.path("../browser/js/config/mediaTypes.json") });
    mod.addAnonymousImport("themeTokens.json", .{ .root_source_file = b.path("../browser/js/config/themeTokens.json") });
    mod.addAnonymousImport("systemPrompt.json", .{ .root_source_file = b.path("../browser/js/config/llm/systemPrompt.json") });
    mod.addAnonymousImport("opRegistry.json", .{ .root_source_file = b.path("../browser/js/config/llm/opRegistry.json") });
    mod.addAnonymousImport("providers.json", .{ .root_source_file = b.path("../browser/js/config/llm/providers.json") });
    // stb's DECODER (untrusted input) keeps UBSan on; regex_shim owns the POSIX regex_t for
    // the --source-name filter (Zig can't embed the opaque translated regex_t by value).
    mod.addCSourceFiles(.{
        .root = b.path("src"),
        .files = &.{ "media/stb_read_impl.c", "scrape/regex_shim.c" },
        .flags = if (small) &.{ "-std=c11", "-O2" } else &.{"-std=c11"},
    });
    // stb's JPEG encoder relies on signed-shift wraparound; Zig instruments C with UBSan in Debug
    // and would trap on it. Its own TU so the exemption never reaches the decoder.
    mod.addCSourceFiles(.{
        .root = b.path("src"),
        .files = &.{"media/stb_write_impl.c"},
        .flags = if (small) &.{ "-std=c11", "-fno-sanitize=undefined", "-O2" } else &.{ "-std=c11", "-fno-sanitize=undefined" },
    });
}

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    // Release packaging passes -Dstrip=true: no debug info in a downloaded binary.
    const strip = b.option(bool, "strip", "Strip debug info from the CLI binary") orelse false;

    // stb_image / stb_image_write: public-domain single-header C codecs (build.zig.zon).
    const stb = b.dependency("stb", .{});

    const exe_mod = b.createModule(.{
        .root_source_file = b.path("src/main.zig"),
        .target = target,
        .optimize = optimize,
        .strip = strip,
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

    // test_root.zig pulls in the inline unit tests (via src/main.zig) and the ones under tests/;
    // rooting at cli/ lets the tests/ files import the src/ modules.
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

    // `zig fmt --check` over every Zig file; `test` depends on it, so CI fails on an unformatted one.
    const fmt = b.addFmt(.{ .paths = &.{ "build.zig", "test_root.zig", "bench_root.zig", "src", "tests" }, .check = true });
    const fmt_step = b.step("fmt", "Check that every Zig file is zig fmt clean (fix with `zig fmt .`)");
    fmt_step.dependOn(&fmt.step);
    test_step.dependOn(&fmt.step);

    // `zig build bench` prints timings, it does not assert them, so it is deliberately out of `test`.
    // ReleaseSmall whatever the top-level optimize choice, so the numbers reflect a shipped build.
    const bench_mod = b.createModule(.{
        .root_source_file = b.path("bench_root.zig"),
        .target = target,
        .optimize = .ReleaseSmall,
    });
    wireNative(b, bench_mod, stb);
    const bench_exe = b.addExecutable(.{ .name = "stencil-bench", .root_module = bench_mod });
    const run_bench = b.addRunArtifact(bench_exe);
    if (b.args) |args| run_bench.addArgs(args);
    const bench_step = b.step("bench", "Run the stencil CLI pipeline benchmark");
    bench_step.dependOn(&run_bench.step);
}
