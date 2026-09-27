//! `/chat` and `/llm` through console.handle: the opt-in chat persistence and who can read a
//! saved chat, and the assistant config resolved from the env with every setter applied.
const std = @import("std");
const console = @import("../../src/console.zig");
const logo = @import("../../src/app/logo.zig");
const Capture = @import("console_harness.zig").Capture;
const testing = std.testing;

test "console: /chat toggles, shows, and clears the opt-in chat persistence" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();

    var session = console.Session{ .gpa = a };
    defer session.deinit();

    // Default OFF (contract §12.2); a bare /chat and '/chat show' only print the state.
    try testing.expect(!session.chat_on);
    try testing.expect(!try console.handle(&session, io, "/chat"));
    try testing.expect(!session.chat_on);
    try testing.expect(!try console.handle(&session, io, "/chat show"));
    try testing.expect(!session.chat_on);

    // on / off / true / false set the toggle explicitly (the /sync grammar).
    _ = try console.handle(&session, io, "/chat on");
    try testing.expect(session.chat_on);
    _ = try console.handle(&session, io, "/chat off");
    try testing.expect(!session.chat_on);
    _ = try console.handle(&session, io, "/chat true");
    try testing.expect(session.chat_on);
    _ = try console.handle(&session, io, "/chat false");
    try testing.expect(!session.chat_on);

    // /chat clear drops the saved turns (no server project active → purely local, no
    // network) and keeps the toggle as it is.
    _ = try console.handle(&session, io, "/chat on");
    try session.appendChatTurn(.user, "crop it");
    try session.appendChatTurn(.assistant, "done");
    try testing.expectEqual(@as(usize, 2), session.chat_history.items.len);
    _ = try console.handle(&session, io, "/chat clear");
    try testing.expectEqual(@as(usize, 0), session.chat_history.items.len);
    try testing.expect(session.chat_on);

    // An unknown argument only errors — nothing changes.
    _ = try console.handle(&session, io, "/chat frobnicate");
    try testing.expect(session.chat_on);
    try testing.expectEqual(@as(usize, 0), session.chat_history.items.len);
}

test "console: /chat on says who can read a saved chat" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();

    var session = console.Session{ .gpa = a };
    defer session.deinit();

    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();

    // §12.2: on a server project the chat file carries the PROJECT's access, which is not what "save chats
    // with the project" promises — so the console says so on the turn that switches saving ON.
    _ = try console.handle(&session, io, "/chat on");
    try testing.expect(std.mem.indexOf(u8, cap.text(), "shared with") != null);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "readable by everyone") != null);
    // Both destinations named: the local .stencil file and the server project.
    try testing.expect(std.mem.indexOf(u8, cap.text(), ".stencil project") != null);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "server project") != null);
}

test "console: /llm resolves the session config from the env and applies every setter" {
    // Wiring only — the config semantics (env defaults, provider→url refill, sub-command
    // grammar) are llm/config.zig's own units.
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();

    // Seed the env the way main.zig does (STENCIL_LLM_* → console.run → session.llm_env).
    var session = console.Session{ .gpa = a, .llm_env = .{ .base_url = "http://box:7777" } };
    defer session.deinit();

    // Bare /llm resolves the config lazily, off llm_env.
    _ = try console.handle(&session, io, "/llm");
    try testing.expect(session.llm_cfg.?.provider == .ollama);
    try testing.expectEqualStrings("http://box:7777", session.llm_cfg.?.base_url);

    // Every setter lands on the resolved config.
    _ = try console.handle(&session, io, "/llm provider openai-compat");
    _ = try console.handle(&session, io, "/llm url http://mine:9/v1/");
    _ = try console.handle(&session, io, "/llm model llava");
    _ = try console.handle(&session, io, "/llm key sk-secret");
    _ = try console.handle(&session, io, "/llm server https://s:8090/");
    try testing.expect(session.llm_cfg.?.provider == .openai_compat);
    try testing.expectEqualStrings("http://mine:9/v1", session.llm_cfg.?.base_url);
    try testing.expectEqualStrings("llava", session.llm_cfg.?.model);
    try testing.expectEqualStrings("sk-secret", session.llm_cfg.?.api_key);
    try testing.expectEqualStrings("https://s:8090", session.llm_cfg.?.server_url);

    // A rejected sub-command and a bare /prompt only print — nothing changes, nothing pushes.
    _ = try console.handle(&session, io, "/llm provider gpt5");
    try testing.expect(session.llm_cfg.?.provider == .openai_compat);
    _ = try console.handle(&session, io, "/llm frobnicate x");
    _ = try console.handle(&session, io, "/prompt");
    try testing.expectEqual(@as(usize, 0), session.stateCount());
}
