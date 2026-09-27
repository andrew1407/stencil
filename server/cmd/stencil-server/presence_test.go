package main

import (
	"context"
	"errors"
	"sync"
	"testing"
	"time"

	"stencil/server/internal/config"
)

// testSettle is the default settle, so the burst below is paced as it is in production.
const testSettle = 500 * time.Millisecond

// recordingBeater reports each beat and whether it ran under a deadline.
type recordingBeater struct {
	beats chan bool
	err   error
}

func (r *recordingBeater) Beat(ctx context.Context) error {
	_, bounded := ctx.Deadline()
	select {
	case r.beats <- bounded:
	case <-ctx.Done():
	}
	return r.err
}

func expectBeat(t *testing.T, r *recordingBeater, what string) {
	t.Helper()
	select {
	case bounded := <-r.beats:
		if !bounded {
			t.Fatalf("the beat %s ran without OP_TIMEOUT's deadline", what)
		}
	case <-time.After(2 * time.Second):
		t.Fatalf("no beat %s", what)
	}
}

// The heartbeat publishes at boot and after a join or leave without waiting for its tick, and the
// shutdown's WaitGroup joins it once the context ends.
func TestPresenceBeatsAtBootAndOnEveryChange(t *testing.T) {
	r := &recordingBeater{beats: make(chan bool, 8)}
	changed := make(chan struct{}, 1)
	ctx, cancel := context.WithCancel(context.Background())
	var wg sync.WaitGroup
	startPresence(ctx, &wg, r, changed, config.PresenceOptions{Heartbeat: time.Hour, Settle: testSettle}, time.Second)

	expectBeat(t, r, "at boot")
	changed <- struct{}{}
	expectBeat(t, r, "after a change")

	cancel()
	done := make(chan struct{})
	go func() { wg.Wait(); close(done) }()
	select {
	case <-done:
	case <-time.After(2 * time.Second):
		t.Fatal("the heartbeat outlived its context")
	}
}

// A failed beat is logged and the next tick tries again: a database blip must not end the heartbeat.
func TestPresenceKeepsBeatingAfterAFailure(t *testing.T) {
	r := &recordingBeater{beats: make(chan bool, 8), err: errors.New("db down")}
	ctx, cancel := context.WithCancel(context.Background())
	var wg sync.WaitGroup
	t.Cleanup(func() { cancel(); wg.Wait() })
	startPresence(ctx, &wg, r, nil, config.PresenceOptions{Heartbeat: 10 * time.Millisecond, Settle: testSettle}, time.Second)
	for i := range 3 {
		expectBeat(t, r, []string{"at boot", "on the first tick", "on the second tick"}[i])
	}
}

// A burst of joins and leaves settles into a beat per PresenceOptions.Settle, not one per change.
func TestPresenceSettlesABurstOfChanges(t *testing.T) {
	r := &recordingBeater{beats: make(chan bool, 64)}
	changed := make(chan struct{}, 1)
	ctx, cancel := context.WithCancel(context.Background())
	var wg sync.WaitGroup
	t.Cleanup(func() { cancel(); wg.Wait() })
	startPresence(ctx, &wg, r, changed, config.PresenceOptions{Heartbeat: time.Hour, Settle: testSettle}, time.Second)
	expectBeat(t, r, "at boot")

	for range 50 {
		select {
		case changed <- struct{}{}:
		default:
		}
		time.Sleep(time.Millisecond)
	}
	expectBeat(t, r, "after the burst")
	time.Sleep(testSettle + 200*time.Millisecond)
	if n := len(r.beats); n > 1 {
		t.Fatalf("a burst of 50 changes took %d more beats, want at most the one left pending", n)
	}
}
