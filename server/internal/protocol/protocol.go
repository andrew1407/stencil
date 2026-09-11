// Package protocol declares the wire DTOs and the WebSocket message envelope
// shared by the server and every Stencil front-end. It is the single source of
// truth the browser/desktop/CLI/extension clients mirror (they re-declare these
// shapes, they do not import them).
//
// ProjectRecord mirrors core/state/projectsStore.hpp ProjectMeta semantics
// (epoch-millisecond timestamps, source = media URL, resource = origin page) and
// adds the server-only storage fields. Per the Stencil parity contract, server/
// is a protocol adapter: it re-declares this shape in Go rather than reaching
// into core/.
package protocol
