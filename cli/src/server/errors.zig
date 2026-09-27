//! What a server call can fail with. Its own module so every wire module under server/ can
//! name it without importing the client back.
const std = @import("std");

pub const Error = error{
    HttpFailed,
    Unauthorized,
    NotFound,
    Conflict, // 409: a stale version on a layout PUT (a peer saved first) — caller retries
    BadResponse,
    NotConnected,
    TlsNotSupported,
    Cancelled, // the console's Ctrl-C ended the wait (see http.watchedRequest)
};

pub const TransportError = Error || std.mem.Allocator.Error;
