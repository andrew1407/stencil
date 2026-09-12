//! The op-plan (llm-contract §1–§3): the JSON a model must answer with, validated strictly
//! before anything executes. This file is the surface llm.zig re-exports; the pieces live
//! under opplan/ — the types, the table-driven validator, the typed normalizer, the §10
//! user-echo guards, the reply-to-JSON extractor and the executor's context helpers.
const model = @import("opplan/model.zig");
const validate = @import("opplan/validate.zig");
const guards = @import("opplan/guards.zig");
const context = @import("opplan/context.zig");

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
pub const resolveAskAnswer = model.resolveAskAnswer;

pub const ParseOutcome = validate.ParseOutcome;
pub const parsePlan = validate.parsePlan;

// §10 user-echo guards (opplan/guards.zig).
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
    _ = @import("opplan/normalize.zig");
    _ = @import("opplan/extract.zig");
    _ = @import("opplan/actions.zig");
    _ = @import("opplan/ask.zig");
}
