// Package hub serves the live connections: every WebSocket and TCP peer says hello, authenticates,
// and then listens to the global project feed. Edits travel over REST under the version guard; nothing
// here carries a layout.
package hub

import (
	"context"
	"sync"

	"stencil/server/internal/auth"
	"stencil/server/internal/eventbus"
	"stencil/server/internal/transport"
)

// Hub owns the set of live connections.
type Hub struct {
	bus      eventbus.Bus
	resolver auth.SessionResolver
	ctx      context.Context
	cancel   context.CancelFunc
	hello    helloGuard // per-IP throttle on failed handshakes (hellolimit.go)
	tune     Tuning     // deadlines and caps (tuning.go)
	perIP    ipCounter  // live connections per client IP (conncap.go)
	feed     *feed      // the one events subscription, fanned out to every connection (feed.go)

	mu    sync.Mutex
	conns map[*connReg]struct{} // live connection cancels, guarded by mu
}

// connReg is one tracked live connection; cancelling its context unwinds the handler (both transports
// honor ctx), draining hijacked WebSocket feeds Shutdown cannot reach. The conn is kept for the notice.
type connReg struct {
	cancel context.CancelFunc
	conn   transport.Conn
}

// New constructs a hub and takes its one subscription to the global feed. Its own context carries ctx's
// values but not its cancellation, so a signal cannot hang up a TCP feed before CloseAll says goodbye.
func New(ctx context.Context, b eventbus.Bus, resolver auth.SessionResolver, opts ...Option) *Hub {
	hctx, cancel := context.WithCancel(context.WithoutCancel(ctx))
	h := &Hub{
		bus:      b,
		resolver: resolver,
		ctx:      hctx,
		cancel:   cancel,
		tune:     defaultTuning,
		conns:    map[*connReg]struct{}{},
	}
	for _, opt := range opts {
		opt(h)
	}
	h.perIP = newIPCounter(h.tune.MaxConnsPerIP)
	h.feed = newFeed(h.tune.FeedBuffer)
	if b == nil {
		h.feed.close()
		return h
	}
	events, stop := b.Subscribe(eventbus.ChannelEvents)
	go h.pump(events, stop)
	return h
}

// trackConn records a live connection's cancel func so CloseAll can reach it,
// returning an untrack func to call when the connection ends.
func (h *Hub) trackConn(conn transport.Conn, cancel context.CancelFunc) func() {
	reg := &connReg{cancel: cancel, conn: conn}
	h.mu.Lock()
	h.conns[reg] = struct{}{}
	h.mu.Unlock()
	return func() {
		h.mu.Lock()
		delete(h.conns, reg)
		h.mu.Unlock()
	}
}
