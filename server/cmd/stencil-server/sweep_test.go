package main

import (
	"context"
	"encoding/json"
	"errors"
	"sync"
	"testing"
	"time"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/protocol"
	"stencil/server/internal/service"
)

// fakeSweepStore holds id→expiresAt rows and counts sweep passes.
type fakeSweepStore struct {
	mu      sync.Mutex
	expires map[string]int64
	calls   int
	err     error // when set, the next DeleteExpiredProjects fails once
}

func (f *fakeSweepStore) DeleteExpiredProjects(_ context.Context, now int64, limit int) ([]string, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.calls++
	if f.err != nil {
		err := f.err
		f.err = nil
		return nil, err
	}
	var ids []string
	for id, exp := range f.expires {
		if len(ids) == limit {
			break
		}
		if exp > 0 && exp <= now {
			ids = append(ids, id)
			delete(f.expires, id)
		}
	}
	return ids, nil
}

func (f *fakeSweepStore) callCount() int {
	f.mu.Lock()
	defer f.mu.Unlock()
	return f.calls
}

func (f *fakeSweepStore) has(id string) bool {
	f.mu.Lock()
	defer f.mu.Unlock()
	_, ok := f.expires[id]
	return ok
}

// sweepDrops builds the service tail the sweep drives, over an in-proc eventbus.
func sweepDrops(fs service.ProjectFiles) *service.ProjectService {
	return sweepDropsOn(fs, eventbus.NewInProc())
}

func sweepDropsOn(fs service.ProjectFiles, b eventbus.Bus) *service.ProjectService {
	return service.NewProjects(nil, fs, nil, b, 0)
}

// fakeRemover records which project directories were dropped, and how many
// removals ran at once (the sweep hands them to a bounded worker pool). It is
// the ProjectFiles half of the service the sweep drives.
type fakeRemover struct {
	mu           sync.Mutex
	removed      []string
	inFlight     int
	peakInFlight int
	delay        time.Duration
}

func (f *fakeRemover) Remove(id string) error {
	f.mu.Lock()
	f.removed = append(f.removed, id)
	f.inFlight++
	if f.inFlight > f.peakInFlight {
		f.peakInFlight = f.inFlight
	}
	f.mu.Unlock()
	time.Sleep(f.delay)
	f.mu.Lock()
	f.inFlight--
	f.mu.Unlock()
	return nil
}

func (f *fakeRemover) peak() int {
	f.mu.Lock()
	defer f.mu.Unlock()
	return f.peakInFlight
}

func (f *fakeRemover) list() []string {
	f.mu.Lock()
	defer f.mu.Unlock()
	return append([]string(nil), f.removed...)
}

// pollUntil retries cond up to ~2s; no bare sleeps as the only synchronization.
func pollUntil(t *testing.T, what string, cond func() bool) {
	t.Helper()
	deadline := time.Now().Add(2 * time.Second)
	for time.Now().Before(deadline) {
		if cond() {
			return
		}
		time.Sleep(5 * time.Millisecond)
	}
	t.Fatalf("timed out waiting for %s", what)
}

// The startup pass deletes an expired project, drops its filestore bytes, and
// publishes a deleted event — while an unexpired project is left alone.
func TestSweepRemovesOnlyExpiredProjects(t *testing.T) {
	st := &fakeSweepStore{expires: map[string]int64{
		"p_old_a":  1, // long past
		"p_new_a":  time.Now().UnixMilli() + int64(time.Hour/time.Millisecond),
		"p_none_a": 0, // no expiry set → never swept
	}}
	fs := &fakeRemover{}
	b := eventbus.NewInProc()
	events, unsub := b.Subscribe(eventbus.ChannelEvents)
	defer unsub()

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	var wg sync.WaitGroup
	startExpirySweep(ctx, &wg, st, sweepDropsOn(fs, b), time.Hour) // only the startup pass fires

	pollUntil(t, "the expired project to be swept", func() bool { return !st.has("p_old_a") })
	if !st.has("p_new_a") || !st.has("p_none_a") {
		t.Fatal("unexpired projects must be untouched")
	}
	pollUntil(t, "the filestore bytes to be dropped", func() bool {
		removed := fs.list()
		return len(removed) == 1 && removed[0] == "p_old_a"
	})

	// The deleted event mirrors the manual-delete shape on the global feed.
	select {
	case env := <-events:
		var msg protocol.WSMessage
		if err := json.Unmarshal(env.Data, &msg); err != nil {
			t.Fatal(err)
		}
		if msg.Type != protocol.WSProjectEv || msg.Event != protocol.EventDeleted ||
			msg.Project == nil || msg.Project.ID != "p_old_a" {
			t.Fatalf("wrong deletion event: %+v", msg)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("no deletion event published")
	}
	select {
	case env := <-events:
		t.Fatalf("unexpected second event: %s", env.Data)
	default:
	}
}

// The sweep runs at startup and again on every tick, and a failing pass is
// logged and survived rather than ending the loop.
func TestSweepRepeatsOnTheTimerAndSurvivesAnError(t *testing.T) {
	st := &fakeSweepStore{expires: map[string]int64{}, err: errors.New("db down")}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	var wg sync.WaitGroup
	startExpirySweep(ctx, &wg, st, sweepDrops(nil), 10*time.Millisecond)

	// Pass 1 (startup) errors; passes 2+ come from the ticker.
	pollUntil(t, "at least three sweep passes", func() bool { return st.callCount() >= 3 })
}

// Cancelling the context stops the loop and the WaitGroup joins.
func TestSweepStopsOnContextCancel(t *testing.T) {
	st := &fakeSweepStore{expires: map[string]int64{}}
	ctx, cancel := context.WithCancel(context.Background())
	var wg sync.WaitGroup
	startExpirySweep(ctx, &wg, st, sweepDrops(nil), 5*time.Millisecond)
	pollUntil(t, "the startup pass", func() bool { return st.callCount() >= 1 })

	cancel()
	done := make(chan struct{})
	go func() { wg.Wait(); close(done) }()
	select {
	case <-done:
	case <-time.After(2 * time.Second):
		t.Fatal("sweep goroutine did not exit after cancel")
	}

	// No further passes run once the loop has exited.
	after := st.callCount()
	time.Sleep(30 * time.Millisecond) // a few would-be ticks
	if st.callCount() != after {
		t.Fatal("sweep kept running after its goroutine exited")
	}
}

// A zero (or negative) interval disables the sweep: no goroutine, no passes.
func TestSweepDisabledByZeroInterval(t *testing.T) {
	st := &fakeSweepStore{expires: map[string]int64{"p_old_a": 1}}
	var wg sync.WaitGroup
	startExpirySweep(context.Background(), &wg, st, sweepDrops(nil), 0)
	wg.Wait() // nothing was ever added
	if st.callCount() != 0 || !st.has("p_old_a") {
		t.Fatal("a disabled sweep must never touch the store")
	}
}
