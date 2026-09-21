// Test entry point for `zig build test`. Rooted at cli/ so it may import both src/
// (the app modules + their inline unit tests) and tests/ (integration tests). The
// integration tests reach the app via "../src/*.zig" and load tests/fixtures/.
test {
    _ = @import("src/main.zig"); // inline unit tests (args, core, image, layout, ...)
    _ = @import("tests/pipeline/pipeline_ops_test.zig");
    _ = @import("tests/script/script_test.zig");
    _ = @import("tests/media/layout_filter_test.zig");
    _ = @import("tests/pipeline/pipeline_e2e_test.zig");
    _ = @import("tests/net/net_guard_test.zig");
    _ = @import("tests/console/console_test.zig");
    _ = @import("tests/help_flags_test.zig");
    _ = @import("tests/console/repl_text_test.zig");
    _ = @import("tests/console/tui_pins_test.zig");
    _ = @import("tests/console/projects_table_test.zig");
    _ = @import("tests/project/project_cli_test.zig");
    _ = @import("tests/llm/opplan_fixtures_test.zig");
    _ = @import("tests/script/script_fixtures_test.zig");
    _ = @import("tests/llm/llm_prompt_core_test.zig");
    _ = @import("tests/llm/llm_prompt_images_test.zig");
    _ = @import("tests/llm/llm_prompt_console_test.zig");
    _ = @import("tests/llm/llm_prompt_ops_test.zig");
    _ = @import("tests/llm/sanitizer_fixtures_test.zig");
    _ = @import("tests/llm/chatdoc_fixtures_test.zig");
    _ = @import("tests/llm/provider_wire_fixtures_test.zig");
    _ = @import("tests/media/layout_fixtures_test.zig");
    _ = @import("tests/project/stencil_project_fixtures_test.zig");
    _ = @import("tests/config/color_names_drift_test.zig");
    _ = @import("tests/config/page_sizes_drift_test.zig");
    _ = @import("tests/config/theme_tokens_drift_test.zig");
    _ = @import("tests/config/media_types_drift_test.zig");
    _ = @import("tests/size_budget_test.zig");
    _ = @import("tests/test_registration_test.zig");
}
