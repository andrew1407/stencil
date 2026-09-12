//! The session's chat transcript (contract §12) and the pending `ask` card (§11) — the
//! conversation state a `/prompt` turn appends to and a fetched project restores.
const Session = @import("../session.zig").Session;
const llm = @import("../../llm.zig");

/// Drop every saved conversation turn (the `/chat clear` local half; §12).
pub fn clearChat(self: *Session) void {
    for (self.chat_history.items) |t| self.gpa.free(t.text);
    self.chat_history.clearRetainingCapacity();
}

/// Append one conversation turn (owned copy of `text`), trimming the history to the
/// most recent 32 turns (the §7/§12 bound) — the same pattern ask_options uses.
pub fn appendChatTurn(self: *Session, role: llm.ChatRole, text: []const u8) !void {
    const dup = try self.gpa.dupe(u8, text);
    errdefer self.gpa.free(dup);
    try self.chat_history.append(self.gpa, .{ .role = role, .text = dup });
    while (self.chat_history.items.len > llm.max_chat_messages) {
        self.gpa.free(self.chat_history.items[0].text);
        _ = self.chat_history.orderedRemove(0);
    }
}

/// Replace the whole history with `turns` (a §12 restore), taking ownership of the
/// slice and its texts (as returned by llm.parseChatDoc).
pub fn adoptChatTurns(self: *Session, turns: []llm.Turn) void {
    self.clearChat();
    defer self.gpa.free(turns);
    self.chat_history.ensureTotalCapacity(self.gpa, turns.len) catch {
        for (turns) |t| self.gpa.free(t.text);
        return;
    };
    for (turns) |t| self.chat_history.appendAssumeCapacity(t);
}

/// Drop the pending `ask` card's options (contract §11) — called when a new card
/// replaces it, when one is answered, and at teardown.
pub fn clearAsk(self: *Session) void {
    for (self.ask_options) |o| self.gpa.free(o);
    if (self.ask_options.len != 0) self.gpa.free(self.ask_options);
    self.ask_options = &.{};
    self.ask_multi = false;
}
