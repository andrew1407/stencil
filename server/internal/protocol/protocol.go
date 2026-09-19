// Package protocol declares the wire DTOs and the WebSocket message envelope shared by the server and
// every Stencil front-end; clients re-declare these shapes rather than importing them.
//
// ProjectRecord mirrors core/state/ProjectsStore.hpp ProjectMeta semantics (epoch-ms timestamps, source =
// media URL, resource = origin page) and adds the server-only storage fields.
package protocol
