//! The op-plan (llm-contract §1–§3): the JSON a model must answer with, validated strictly by
//! core/opplan before anything executes. This file is the surface llm.zig re-exports; the pieces
//! live under plan/ — the types, core's result mapped to them, the §10 user-echo guards and the
//! executor's context helpers.
const model = @import("plan/model.zig");
const validate = @import("plan/validate.zig");
const guards = @import("plan/guards.zig");
const context = @import("plan/context.zig");

pub const max_attachments = model.max_attachments;
pub const max_label_chars = model.max_label_chars;
pub const max_image_bytes = model.max_image_bytes;
pub const Dir = model.Dir;
pub const FilterMode = model.FilterMode;
pub const CropEdges = model.CropEdges;
pub const Action = model.Action;
pub const Variant = model.Variant;
pub const AskOption = model.AskOption;
pub const Ask = model.Ask;
pub const Plan = model.Plan;
pub const findOp = model.findOp;
pub const resolveAskAnswer = @import("plan/ask.zig").resolveAskAnswer;

pub const ParseOutcome = validate.ParseOutcome;
pub const parsePlan = validate.parsePlan;

// §10 user-echo guards (plan/guards.zig).
pub const understoodPath = guards.understoodPath;
pub const urlEchoedByUser = guards.urlEchoedByUser;
pub const pathEchoedByUser = guards.pathEchoedByUser;

pub const cropSpecString = context.cropSpecString;
pub const ServerMatch = context.ServerMatch;
pub const resolveServer = context.resolveServer;
pub const ConsoleServer = context.ConsoleServer;
pub const max_context_projects = context.max_context_projects;
pub const consoleContextAlloc = context.consoleContextAlloc;
pub const sanitizeLabel = context.sanitizeLabel;

test {
    _ = model;
    _ = validate;
    _ = guards;
    _ = context;
    _ = @import("plan/normalize.zig");
    _ = @import("plan/ask.zig");
}
