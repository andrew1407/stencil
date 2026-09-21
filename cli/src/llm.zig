//! LLM assistant support for the console (`/prompt`, `/llm`) — the CLI's port of `llm-contract.md`.
//! Facade only: the implementation lives in llm/ — registry + prompt assembly (§4/§13), provider
//! config (§5), wire mappings + reply extraction (§6), transport, and the strict op-plan parser
//! (§1–§3, table-driven from the embedded opRegistry.json), mirroring browser/js/llm/plan/plan.js and
//! mcp/src/. Transport + parsing only; the handlers execute actions through existing operations.
const registry = @import("llm/registry.zig");
const opSchema = @import("llm/opSchema.zig");
const config = @import("llm/config.zig");
const wire = @import("llm/wire.zig");
const transport = @import("llm/transport.zig");
const opplan = @import("llm/opplan.zig");

// The contract-§1 caps live in the embedded registry: `schema().limitNamed("MAX_ACTIONS")`.
pub const Schema = opSchema.Schema;
pub const schema = opSchema.get;
pub const max_attachments = opplan.max_attachments;
pub const max_label_chars = opplan.max_label_chars;
pub const max_image_bytes = opplan.max_image_bytes;
pub const OpCapability = registry.OpCapability;
pub const OpCaps = registry.OpCaps;
pub const full_capabilities = registry.full_capabilities;
pub const OpDescriptor = registry.OpDescriptor;
pub const op_registry = registry.op_registry;
pub const isForbiddenOp = registry.isForbiddenOp;
pub const sensitive_patterns = registry.sensitive_patterns;
pub const bulletIsSensitive = registry.bulletIsSensitive;
pub const opsSection = registry.opsSection;
pub const consoleBlock = registry.consoleBlock;
pub const systemPrompt = registry.systemPrompt;
pub const console_settings_prompt = registry.console_settings_prompt;
pub const consoleSystemPrompt = registry.consoleSystemPrompt;
pub const edge_map_suffix = registry.edge_map_suffix;
pub const Provider = config.Provider;
pub const Env = config.Env;
pub const Config = config.Config;
pub const Cmd = config.Cmd;
pub const parseCmd = config.parseCmd;
pub const max_chat_messages = wire.max_chat_messages;
pub const ChatRole = wire.ChatRole;
pub const Turn = wire.Turn;
pub const continuation_note = wire.continuation_note;
pub const chatDisplayText = wire.chatDisplayText;
pub const chatDocAlloc = wire.chatDocAlloc;
pub const parseChatDoc = wire.parseChatDoc;
pub const freeTurns = wire.freeTurns;
pub const Request = wire.Request;
pub const buildRequest = wire.buildRequest;
pub const buildRequestWithHistory = wire.buildRequestWithHistory;
pub const buildRequestWithSystem = wire.buildRequestWithSystem;
pub const PostError = transport.PostError;
pub const request_timeout_ms = transport.request_timeout_ms;
pub const Waiter = transport.Waiter;
pub const postJson = transport.postJson;
pub const isLlmDisabled = transport.isLlmDisabled;
pub const errorDetail = transport.errorDetail;
pub const sanitizeDetail = transport.sanitizeDetail;
pub const Extracted = wire.Extracted;
pub const extractReply = wire.extractReply;
pub const Dir = opplan.Dir;
pub const FilterMode = opplan.FilterMode;
pub const CropEdges = opplan.CropEdges;
pub const Action = opplan.Action;
pub const Variant = opplan.Variant;
pub const AskOption = opplan.AskOption;
pub const Ask = opplan.Ask;
pub const Plan = opplan.Plan;
pub const resolveAskAnswer = opplan.resolveAskAnswer;
pub const ParseOutcome = opplan.ParseOutcome;
pub const parsePlan = opplan.parsePlan;
pub const understoodPath = opplan.understoodPath;
pub const urlEchoedByUser = opplan.urlEchoedByUser;
pub const pathEchoedByUser = opplan.pathEchoedByUser;
pub const cropSpecString = opplan.cropSpecString;
pub const ServerMatch = opplan.ServerMatch;
pub const resolveServer = opplan.resolveServer;
pub const ConsoleServer = opplan.ConsoleServer;
pub const max_context_projects = opplan.max_context_projects;
pub const consoleContextAlloc = opplan.consoleContextAlloc;
pub const sanitizeLabel = opplan.sanitizeLabel;

test {
    _ = registry;
    _ = opSchema;
    _ = config;
    _ = wire;
    _ = transport;
    _ = opplan;
}
