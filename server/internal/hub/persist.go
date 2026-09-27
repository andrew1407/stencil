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
	persistLoad persistKind = iota // GetProjectSnapshot
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

// persistResult is the outcome of a persistJob, posted back to the run-loop so state mutations
// (version/snapshot) stay single-owner — applied on the run-loop, never by the worker.
type persistResult struct {
	kind   persistKind
	member *member
	rec    protocol.ProjectRecord // a save's is metadata only
	layout json.RawMessage        // the saved layout, which the write did not read back
	err    error
}

// snapshotWorker is the only part of a session that touches the store, so the only part needing a per-op
// deadline. It owns no session state: it drains jobs, runs the store call, and posts the outcome back.
type snapshotWorker struct {
	ctx       context.Context // bounds every store call (the hub's lifetime)
	store     Store
	projectID string
	timeout   time.Duration // deadline around ONE store operation

	jobs    chan persistJob
	results chan persistResult
	done    <-chan struct{} // closed when the session tears down
}

func newSnapshotWorker(ctx context.Context, st Store, projectID string, tune Tuning, done <-chan struct{}) *snapshotWorker {
	return &snapshotWorker{
		ctx: ctx, store: st, projectID: projectID, timeout: tune.OpTimeout,
		jobs:    make(chan persistJob, tune.OutBuffer),
		results: make(chan persistResult, tune.OutBuffer),
		done:    done,
	}
}

// run is the worker goroutine. Exactly one runs per session, and that is the version-ordering guarantee:
// jobs complete in dispatch order, so a save cannot overtake the first load.
func (w *snapshotWorker) run() {
	for {
		select {
		case <-w.done:
			return
		case job := <-w.jobs:
			ctx, cancel := context.WithTimeout(w.ctx, w.timeout)
			switch job.kind {
			case persistLoad:
				rec, err := w.store.GetProjectSnapshot(ctx, w.projectID)
				w.post(persistResult{kind: persistLoad, rec: rec, err: err})
			case persistSave:
				rec, err := w.store.UpdateProject(ctx, w.projectID, store.ProjectPatch{Layout: job.layout}, job.version)
				w.post(persistResult{kind: persistSave, member: job.member, rec: rec, layout: job.layout, err: err})
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

// ensureLoaded kicks off a snapshot load when none has succeeded, or a reported write made the cached
// one stale, and none is in flight. Idempotent; safe to call from any run-loop case.
func (s *session) ensureLoaded() {
	if s.loadInFlight || (s.loaded && !s.stale) {
		return
	}
	s.loadInFlight = true
	s.persist.dispatch(persistJob{kind: persistLoad})
}

// refresh marks the snapshot stale once the feed reports a version past the session's (our own save's
// echo is not). A read already in flight may predate that write, so another follows it.
func (s *session) refresh(v int64) {
	if v <= s.version {
		return
	}
	s.stale = true
	s.reread = s.loadInFlight
	s.ensureLoaded()
}

// applyResult applies an off-loop store outcome on the run-loop, keeping all
// state mutations single-owner.
func (s *session) applyResult(res persistResult) {
	switch res.kind {
	case persistLoad:
		s.loadInFlight = false
		if res.err != nil {
			// A later join retries (never an edit). Pending welcomes still get a reply below: the stale
			// snapshot, or an empty record + version 0 before the first.
			log.Printf("hub: load project %s failed: %v", s.id, res.err)
		} else {
			s.loadedRec = res.rec
			s.version = max(s.version, res.rec.Version)
			s.loaded, s.stale = true, s.reread
		}
		if s.reread {
			s.reread = false
			s.ensureLoaded() // the waiting welcomes take the next read
			return
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
