//! The console's LLM assistant (`/prompt`, `/llm`) — the CLI's execution half of
//! llm-contract.md. Transport and op-plan PARSING live in llm/; this package turns a
//! validated plan into the console's existing operations. This file is the surface
//! console.zig binds to; the pieces live under llm/.
const config = @import("llm/config.zig");
const run = @import("llm/run.zig");
const plan = @import("llm/plan.zig");
const fixture = @import("llm/fixture.zig");

pub const doLlm = config.doLlm;
pub const doPrompt = run.doPrompt;
pub const applyPlanAction = plan.applyPlanAction;

// Test fixtures (llm/fixture.zig), reached through this name by the console's own suites.
pub const Capture = fixture.Capture;
pub const swallowPrint = fixture.swallowPrint;
pub const pngOf = fixture.pngOf;

test {
    _ = config;
    _ = run;
    _ = plan;
    _ = fixture;
    _ = @import("llm/attach.zig");
    _ = @import("llm/edits.zig");
    _ = @import("llm/settings.zig");
    _ = @import("llm/planOps.zig");
    _ = @import("llm/variants.zig");
}
