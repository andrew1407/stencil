package hub

import (
	"context"
	"encoding/json"
	"sync/atomic"
	"syscall"
	"testing"
	"time"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/protocol"
)

// brokenPeer says a good hello, then fails every write as a reset socket does. It counts its Closes
// and the Reads still blocked on it.
type brokenPeer struct {
	hello   chan []byte
	reading atomic.Int32
	closes  atomic.Int32
}

func newBrokenPeer(t *testing.T) *brokenPeer {
	raw, err := json.Marshal(protocol.WSMessage{Type: protocol.WSHello, Token: goodToken})
	if err != nil {
		t.Fatal(err)
	}
	p := &brokenPeer{hello: make(chan []byte, 1)}
	p.hello <- raw
	return p
}

func (p *brokenPeer) Read(ctx context.Context) ([]byte, error) {
	select {
	case raw := <-p.hello:
		return raw, nil
	default:
	}
	p.reading.Add(1)
	defer p.reading.Add(-1)
	<-ctx.Done()
	return nil, ctx.Err()
}

func (p *brokenPeer) Write(context.Context, []byte) error { return syscall.EPIPE }
func (p *brokenPeer) SetReadLimit(int64)                  {}
func (p *brokenPeer) Close(int, string) error             { p.closes.Add(1); return nil }
func (p *brokenPeer) RemoteAddr() string                  { return "127.0.0.1:1" }

// A feed write that fails ends the handler, and the handler still closes the socket and ends its
// reader: nothing of the connection outlives it.
func TestAFailedFeedWriteHangsUpTheConnection(t *testing.T) {
	h := newTestHub(t)
	peer := newBrokenPeer(t)
	done := make(chan error, 1)
	go func() { done <- h.HandleConn(context.Background(), peer) }()
	waitFor(t, func() bool { return peer.reading.Load() == 1 }) // the reader starts once the feed listens

	eventbus.PublishProjectEvent(context.Background(), h.bus, protocol.EventUpdated, protocol.ProjectRecord{ID: "p_probe_1"})
	select {
	case err := <-done:
		if err == nil {
			t.Fatal("a failed write should end the handler with its error")
		}
	case <-time.After(3 * time.Second):
		t.Fatal("the handler outlived a failed write")
	}
	if n := peer.closes.Load(); n == 0 {
		t.Fatal("the handler returned without closing the connection")
	}
	waitFor(t, func() bool { return peer.reading.Load() == 0 && liveConnCount(h) == 0 })
}
