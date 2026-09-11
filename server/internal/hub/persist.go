package hub

// The session's DB arm. Saving and snapshot-loading are the only blocking store
// calls in a live session, so they run on their own goroutine and post outcomes
// back to the run-loop, which stays the single owner of session state.

import (
	"context"
	"encoding/json"
	"log"
	"time"

	"stencil/server/internal/protocol"
	"stencil/server/internal/store"
)

// persistKind tags an off-loop store operation dispatched to the worker.
type persistKind int

const (
	persistLoad persistKind = iota // GetProject (snapshot)
	persistSave                    // UpdateProject (save)
)

// persistJob is a unit of blocking store I/O handed from the run-loop to the
// worker goroutine. The run-loop never blocks on the store itself.
type persistJob struct {
	kind    persistKind
	member  *member         // save requester (for the ack/error reply); nil for load
	layout  json.RawMessage // save payload
	version int64           // save expected version (LWW guard)
}

// persistResult is the outcome of a persistJob, posted back to the run-loop so
// state mutations (version/snapshot) stay single-owner. It is applied on the
// run-loop, never by the worker.
type persistResult struct {
	kind   persistKind
	member *member
	rec    protocol.ProjectRecord
	err    error
}

// snapshotWorker is the only part of a session that touches the store, and so
// the only part that needs a per-op deadline. It owns no session state: it
// drains jobs, runs the blocking store call, and posts the outcome back.
type snapshotWorker struct {
	ctx       context.Context // bounds every store call (the hub's lifetime)
	store     Store
	projectID string
	timeout   time.Duration // deadline around ONE store operation

	jobs    chan persistJob
	results chan persistResult
	done    <-chan struct{} // closed when the session tears down
}

func newSnapshotWorker(ctx context.Context, st Store, projectID string, timeout time.Duration, done <-chan struct{}) *snapshotWorker {
	return &snapshotWorker{
		ctx: ctx, store: st, projectID: projectID, timeout: timeout,
		jobs:    make(chan persistJob, outBuffer),
		results: make(chan persistResult, outBuffer),
		done:    done,
	}
}

// run is the worker goroutine. It exits when the session tears down.
func (w *snapshotWorker) run() {
	for {
		select {
		case <-w.done:
			return
		case job := <-w.jobs:
			ctx, cancel := context.WithTimeout(w.ctx, w.timeout)
			switch job.kind {
			case persistLoad:
				rec, err := w.store.GetProject(ctx, w.projectID)
				w.post(persistResult{kind: persistLoad, rec: rec, err: err})
			case persistSave:
				rec, err := w.store.UpdateProject(ctx, w.projectID, store.ProjectPatch{Layout: job.layout}, job.version)
				w.post(persistResult{kind: persistSave, member: job.member, rec: rec, err: err})
			}
			cancel()
		}
	}
}

// dispatch hands a job to the worker without blocking the run-loop past teardown.
func (w *snapshotWorker) dispatch(job persistJob) {
	select {
	case w.jobs <- job:
	case <-w.done:
	}
}

// post hands an outcome back to the run-loop; it unblocks if the session is
// already tearing down so the worker never leaks.
func (w *snapshotWorker) post(r persistResult) {
	select {
	case w.results <- r:
	case <-w.done:
	}
}

// ----- the run-loop's half -----

// ensureLoaded kicks off the one-time snapshot load if it has not succeeded and
// is not already in flight. Idempotent; safe to call from any run-loop case.
func (s *session) ensureLoaded() {
	if s.loaded || s.loadInFlight {
		return
	}
	s.loadInFlight = true
	s.persist.dispatch(persistJob{kind: persistLoad})
}

// applyResult applies an off-loop store outcome on the run-loop, keeping all
// state mutations single-owner.
func (s *session) applyResult(res persistResult) {
	switch res.kind {
	case persistLoad:
		s.loadInFlight = false
		if res.err != nil {
			// Load failed; leave loaded=false so a later join retries (never per
			// edit). Pending welcomes still get a reply below, with an empty
			// record + version 0.
			log.Printf("hub: load project %s failed: %v", s.id, res.err)
		} else {
			s.loadedRec = res.rec
			if !s.loaded {
				s.version = res.rec.Version
				s.loaded = true
			}
		}
		pending := s.pendingWelcome
		s.pendingWelcome = nil
		for _, m := range pending {
			s.replyWelcome(m)
		}
	case persistSave:
		s.applySaveResult(res)
	}
}
