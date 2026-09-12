//! The §6 request BODY writers: one per provider shape, sharing the chat-envelope open
//! and close so the system turn, the replayed history and the current turn always sit in
//! the same order.
const std = @import("std");
const config = @import("../config.zig");
const chatDoc = @import("chatDoc.zig");

const Config = config.Config;
const Turn = chatDoc.Turn;
const boundedHistory = chatDoc.boundedHistory;

pub fn writeBody(js: *std.json.Stringify, cfg: *const Config, system: []const u8, prompt: []const u8, images: []const []const u8, history: []const Turn) std.json.Stringify.Error!void {
    switch (cfg.provider) {
        // §6.1 — native Ollama chat: images ride as bare base64 strings, in order.
        .ollama => {
            try openChatBody(js, cfg, system, history);
            try js.write(prompt);
            if (images.len != 0) {
                try js.objectField("images");
                try js.beginArray();
                for (images) |b64| try js.write(b64);
                try js.endArray();
            }
            try closeChatBody(js);
        },
        // §6.2 — OpenAI-compatible: one data-URL content part per image, after the text.
        .openai_compat => {
            try openChatBody(js, cfg, system, history);
            if (images.len != 0) {
                try js.beginArray();
                try js.beginObject();
                try js.objectField("type");
                try js.write("text");
                try js.objectField("text");
                try js.write(prompt);
                try js.endObject();
                for (images) |b64| {
                    try js.beginObject();
                    try js.objectField("type");
                    try js.write("image_url");
                    try js.objectField("image_url");
                    try js.beginObject();
                    try js.objectField("url");
                    // Streamed raw to skip an intermediate data-URL copy of the image; base64
                    // never needs JSON escaping, so the bytes match js.write of the same string.
                    try js.beginWriteRaw();
                    try js.writer.writeAll("\"data:image/png;base64,");
                    try js.writer.writeAll(b64);
                    try js.writer.writeAll("\"");
                    js.endWriteRaw();
                    try js.endObject();
                    try js.endObject();
                }
                try js.endArray();
            } else {
                try js.write(prompt);
            }
            try closeChatBody(js);
        },
        // §6.3 — the collaboration server's Anthropic proxy (protocol.LlmChatRequest).
        .stencil_server => {
            try js.beginObject();
            try js.objectField("system");
            try js.write(system);
            try js.objectField("messages");
            try js.beginArray();
            // Replayed history rides before the current turn, text-only (§7/§12).
            for (history) |t| {
                try js.beginObject();
                try js.objectField("role");
                try js.write(@tagName(t.role));
                try js.objectField("text");
                try js.write(t.text);
                try js.endObject();
            }
            try js.beginObject();
            try js.objectField("role");
            try js.write("user");
            try js.objectField("text");
            try js.write(prompt);
            if (images.len != 0) {
                try js.objectField("images");
                try js.beginArray();
                for (images) |b64| {
                    try js.beginObject();
                    try js.objectField("mediaType");
                    try js.write("image/png");
                    try js.objectField("data");
                    try js.write(b64);
                    try js.endObject();
                }
                try js.endArray();
            }
            try js.endObject();
            try js.endArray();
            if (cfg.model.len != 0) {
                try js.objectField("model");
                try js.write(cfg.model);
            }
            try js.endObject();
        },
    }
}

fn writeRoleContent(js: *std.json.Stringify, role: []const u8, content: []const u8) std.json.Stringify.Error!void {
    try js.beginObject();
    try js.objectField("role");
    try js.write(role);
    try js.objectField("content");
    try js.write(content);
    try js.endObject();
}

/// The shared §6.1/§6.2 chat-body prologue: model/stream/messages + system + history +
/// the user message opened up to its `content` value (where the two mappings differ).
/// Balanced by `closeChatBody`.
fn openChatBody(js: *std.json.Stringify, cfg: *const Config, system: []const u8, history: []const Turn) std.json.Stringify.Error!void {
    try js.beginObject();
    try js.objectField("model");
    try js.write(cfg.model);
    try js.objectField("stream");
    try js.write(false);
    try js.objectField("messages");
    try js.beginArray();
    try writeRoleContent(js, "system", system);
    for (history) |t| try writeRoleContent(js, @tagName(t.role), t.text);
    try js.beginObject();
    try js.objectField("role");
    try js.write("user");
    try js.objectField("content");
}

fn closeChatBody(js: *std.json.Stringify) std.json.Stringify.Error!void {
    try js.endObject(); // the user message
    try js.endArray(); // messages
    try js.endObject(); // the body
}
