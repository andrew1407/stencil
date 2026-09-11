//! §2.1 attachments: the images a `/prompt` turn carries. `attachments` are the turn's
//! adopted uploads; `pending` is the drop the user has not confirmed yet.
const Session = @import("../session.zig").Session;
const max_pending = Session.max_pending;
const image = @import("../../image.zig");
const llm = @import("../../llm.zig");
const Attachment = @import("../session.zig").Attachment;


// §2.1 turn attachments
/// Remember an uploaded image as an attachment of the current turn (taking ownership
/// of `label_src`'s copy and `bytes`). A previous turn's list is dropped first, so
/// `/upload a` `/upload b` `/prompt …` attaches exactly a and b; past
/// `max_attachments` the oldest falls off, keeping the newest §7-many.
pub fn addAttachment(self: *Session, label_src: []const u8, bytes: []u8, fmt: image.Format, temp: bool) !void {
    if (self.attachments_used) self.clearAttachments();
    const label = self.gpa.dupe(u8, label_src) catch |e| {
        self.gpa.free(bytes);
        return e;
    };
    self.attachments.append(self.gpa, .{ .label = label, .bytes = bytes, .fmt = fmt, .temp = temp }) catch |e| {
        self.gpa.free(label);
        self.gpa.free(bytes);
        return e;
    };
    if (self.attachments.items.len > llm.max_attachments) {
        var oldest = self.attachments.orderedRemove(0);
        oldest.deinit(self.gpa);
    }
}

/// Take back the newest attachment of the CURRENT turn, handing it to the caller (who
/// deinits it). Null when the turn has none — including when a `/prompt` already spent
/// them: that turn is over, so there is nothing left to take back.
pub fn popAttachment(self: *Session) ?Attachment {
    if (self.attachments_used or self.attachments.items.len == 0) return null;
    return self.attachments.pop();
}

/// The images this turn will send, in attachment order (empty once a /prompt spent them).
pub fn liveAttachments(self: *Session) []const Attachment {
    return if (self.attachments_used) &.{} else self.attachments.items;
}

/// Take back attachment `idx` (0-based) — `/unpaste <n>`. Caller deinits it.
pub fn removeAttachment(self: *Session, idx: usize) ?Attachment {
    if (self.attachments_used or idx >= self.attachments.items.len) return null;
    return self.attachments.orderedRemove(idx);
}

/// The newest attachment of the current turn, borrowed — null under `popAttachment`'s rules.
pub fn lastAttachment(self: *Session) ?*const Attachment {
    if (self.attachments_used or self.attachments.items.len == 0) return null;
    return &self.attachments.items[self.attachments.items.len - 1];
}

/// Mark the turn's attachments as spent (called once a /prompt turn has used them):
/// the next `/upload` starts a new turn's list.
pub fn consumeAttachments(self: *Session) void {
    if (self.attachments.items.len != 0) self.attachments_used = true;
}

pub fn clearAttachments(self: *Session) void {
    for (self.attachments.items) |*at| at.deinit(self.gpa);
    self.attachments.clearRetainingCapacity();
    self.attachments_used = false;
}

/// Hold a pasted image against the line being edited (taking ownership of `bytes`).
/// Nothing else in the session sees it until the line is submitted or abandoned.
pub fn addPending(self: *Session, label_src: []const u8, bytes: []u8, fmt: image.Format, temp: bool) !void {
    const label = self.gpa.dupe(u8, label_src) catch |e| {
        self.gpa.free(bytes);
        return e;
    };
    self.pending.append(self.gpa, .{ .label = label, .bytes = bytes, .fmt = fmt, .temp = temp }) catch |e| {
        self.gpa.free(label);
        self.gpa.free(bytes);
        return e;
    };
}

/// Keep only the pending images at `kept` (0-based, in the order the line's markers now
/// read) and drop the rest — how the editor reports a marker the user deleted or moved.
pub fn keepPending(self: *Session, kept: []const usize) void {
    var out: [max_pending]Attachment = undefined;
    var taken = [_]bool{false} ** max_pending;
    var n: usize = 0;
    for (kept) |i| {
        if (i >= self.pending.items.len or i >= max_pending or taken[i] or n == out.len) continue;
        taken[i] = true;
        out[n] = self.pending.items[i];
        n += 1;
    }
    for (self.pending.items, 0..) |*at, i| {
        if (i < max_pending and taken[i]) continue; // moved into `out`, not ours to free
        at.deinit(self.gpa);
    }
    self.pending.clearRetainingCapacity();
    self.pending.appendSliceAssumeCapacity(out[0..n]); // n ≤ what we just cleared
}

pub fn clearPending(self: *Session) void {
    self.keepPending(&.{});
}

/// Hand the line's pending images over: the caller owns every one it receives (the
/// session keeps none), which is how a submitted line turns them into uploads.
pub fn takePending(self: *Session, out: []Attachment) usize {
    const n = @min(self.pending.items.len, out.len);
    @memcpy(out[0..n], self.pending.items[0..n]);
    for (self.pending.items[n..]) |*at| at.deinit(self.gpa); // more than `out` holds: dropped
    self.pending.clearRetainingCapacity();
    return n;
}
