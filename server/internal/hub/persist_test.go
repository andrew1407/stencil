package hub

import (
	"context"
	"testing"
	"time"

	"stencil/server/internal/bus"
	"stencil/server/internal/protocol"
	"stencil/server/internal/store"
)

// gatedStore holds the snapshot load open while a save returns at once, so a
// worker that ran the two concurrently would finish them out of order.
type gatedStore struct {
	entered chan struct{}
	release chan struct{}
	saving  chan struct{}
}

func (g *gatedStore) GetProject(context.Context, string) (protocol.ProjectRecord, error) {
	g.entered <- struct{}{}
	<-g.release
	return protocol.ProjectRecord{ID: "p_t_a", Version: 3}, nil
}

func (g *gatedStore) UpdateProject(_ context.Context, _ string, _ store.ProjectPatch, expected int64) (protocol.ProjectRecord, error) {
	g.saving <- struct{}{}
	return protocol.ProjectRecord{ID: "p_t_a", Version: expected + 1}, nil
}

// A session's ONE snapshot worker is the version-ordering invariant: results
// come back in dispatch order, so a quick save cannot overtake the first load —
// whose result would then re-adopt the pre-save version (applyResult sets
// s.version from a load while s.loaded is false). A second worker needs a
// monotonic version guard first; this test is what fails without one.
func TestSnapshotWorkerCompletesJobsInDispatchOrder(t *testing.T) {
	g := &gatedStore{entered: make(chan struct{}, 1), release: make(chan struct{}), saving: make(chan struct{}, 1)}
	done := make(chan struct{})
	defer close(done)
	w := newSnapshotWorker(context.Background(), g, "p_t_a", time.Second, done)
	go w.run()

	w.dispatch(persistJob{kind: persistLoad})
	<-g.entered // the load is now inside the store call
	w.dispatch(persistJob{kind: persistSave, version: 7})
	select {
	case <-g.saving:
		t.Fatal("the save ran while the first load was still in flight")
	case <-time.After(100 * time.Millisecond):
	}
	close(g.release)

	if first := <-w.results; first.kind != persistLoad || first.rec.Version != 3 {
		t.Fatalf("first result: kind %v version %d, want the load at 3", first.kind, first.rec.Version)
	}
	if second := <-w.results; second.kind != persistSave || second.rec.Version != 8 {
		t.Fatalf("second result: kind %v version %d, want the save at 8", second.kind, second.rec.Version)
	}
}

// The run-loop reports the version the store committed, not the one the saver
// guessed, and keeps the cached snapshot in step with it.
func TestApplySaveResultAdoptsTheCommittedVersion(t *testing.T) {
	s := &session{id: "p_t_a", members: map[string]*member{}, version: 3}
	m := newMember("c1", "A", nil)
	s.members["c1"] = m
	s.hub = &Hub{ctx: context.Background(), bus: bus.NewInProc()}

	s.applySaveResult(persistResult{kind: persistSave, member: m,
		rec: protocol.ProjectRecord{ID: "p_t_a", Version: 9, ResultPath: "p/result.png"}})
	if s.version != 9 || s.loadedRec.Version != 9 {
		t.Fatalf("version %d, cached %d, want 9", s.version, s.loadedRec.Version)
	}
}
