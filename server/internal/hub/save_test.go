package hub

// Saves behind a slow store: the run loop keeps answering, and a save still waiting when a newer one
// arrives is superseded rather than queued, so one session never holds more than two layouts.

import (
	"encoding/json"
	"testing"

	"stencil/server/internal/protocol"
)

func TestASlowSaveNeitherStallsTheRunLoopNorQueuesEverySave(t *testing.T) {
	h, held, _ := newHeldHub(t)
	a := joinProject(t, startTCP(t, h), "p_t_a", "A")
	send(t, a, protocol.WSMessage{Type: protocol.WSSave, Version: 0, Layout: json.RawMessage(`{"n":1}`)})
	<-held.entered
	send(t, a, protocol.WSMessage{Type: protocol.WSSave, Version: 1, Layout: json.RawMessage(`{"n":2}`)})
	send(t, a, protocol.WSMessage{Type: protocol.WSSave, Version: 1, Layout: json.RawMessage(`{"n":3}`)})
	if e := readUntil(t, a, protocol.WSError); e.Code != protocol.CodeConflict {
		t.Fatalf("the superseded save got %+v, want conflict", e)
	}
	send(t, a, protocol.WSMessage{Type: protocol.WSPing})
	readUntil(t, a, protocol.WSPong) // answered while the store still holds the first save

	close(held.release)
	if s := readUntil(t, a, protocol.WSSynced); s.Version != 1 {
		t.Fatalf("first synced at %d, want 1", s.Version)
	}
	waitFor(t, func() bool {
		rec, _ := held.Project("p_t_a")
		return rec.Version == 2 && string(rec.Layout) == `{"n":3}`
	})
	if n := len(held.entered); n != 1 {
		t.Fatalf("%d more saves reached the store, want only the newest", n)
	}
}

func TestSaveQueueHoldsOneInFlightAndOneWaiting(t *testing.T) {
	s := &session{id: "p_t_a", members: map[string]*member{},
		persist: &snapshotWorker{jobs: make(chan persistJob, 8)}}
	m := newMember("A", "A", nil, defaultTuning)
	s.members["A"] = m
	for v := range 5 {
		s.handleSave(m, protocol.WSMessage{Type: protocol.WSSave, Version: int64(v)})
	}
	if len(s.persist.jobs) != 1 || s.pendingSave == nil || s.pendingSave.version != 4 {
		t.Fatalf("%d dispatched, pending %+v; want one in flight and the newest waiting", len(s.persist.jobs), s.pendingSave)
	}
	if len(m.out) != 3 {
		t.Fatalf("%d superseded answers, want 3", len(m.out))
	}
	<-s.persist.jobs
	s.nextSave()
	if job := <-s.persist.jobs; job.version != 4 || s.pendingSave != nil || !s.saveInFlight {
		t.Fatalf("next dispatched %d, pending %v; want the newest and an empty slot", job.version, s.pendingSave)
	}
}
