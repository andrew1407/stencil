//! Live server-project sync for the console: the events feed poll (peer pulls,
//! metadata refresh, dirty-warn), the /sync debounce, and the layout/result/chat
//! push + adopt helpers shared by /save, /fetch and the flush.
const poll = @import("remoteEvents/poll.zig");
const push = @import("remoteEvents/push.zig");

pub const PullAction = poll.PullAction;
pub const pullAction = poll.pullAction;
pub const pollEvents = poll.pollEvents;

pub const markDirty = push.markDirty;
pub const shouldFlush = push.shouldFlush;
pub const flushSync = push.flushSync;
pub const pushResult = push.pushResult;
pub const restoreServerChat = push.restoreServerChat;
pub const adoptServerLayout = push.adoptServerLayout;

test {
    _ = poll;
    _ = push;
}
