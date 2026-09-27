// Package testutil holds the in-memory fakes shared by the server's test
// suites (httpapi, hub). Everything here builds only on exported APIs of the
// packages it stands in for, so it works from any test package.
package testutil

import (
	"context"
	"strconv"
	"sync"

	"stencil/server/internal/auth"
	"stencil/server/internal/protocol"
	"stencil/server/internal/store"
)

// MemStore is an in-memory project + session store (sessions.go). It satisfies httpapi.ProjectStore,
// httpapi.SessionStore, service.ProjectStore and hub.Store.
type MemStore struct {
	mu       sync.Mutex
	projects map[string]protocol.ProjectRecord
	sessions map[string]auth.Session
	seq      int
	// setFileCalls counts SetFile invocations, so tests can assert that
	// filestore-only kinds (video/variantN) never touch the project record.
	setFileCalls int
	// sweptOnWrite[id]=true makes SetFile report the row gone while GetProject still sees it — the sweep
	// deleting the project between the upload handler's existence check and its SetFile write.
	sweptOnWrite map[string]bool
	// getsUntilGone[id]=N lets a project read succeed N more times, then report the row gone — the sweep
	// firing between the pre-check and the post-write re-check for filestore-only kinds.
	getsUntilGone map[string]int
	reads         int // project reads of any kind, so a test can bound them
	charges       map[chargeKey]memCharge
	ledgerCalls   int // Admit and Credit calls, so a test can prove the ledger untouched
}

// NewMemStore returns an empty in-memory store.
func NewMemStore() *MemStore {
	return &MemStore{
		projects:      map[string]protocol.ProjectRecord{},
		sessions:      map[string]auth.Session{},
		sweptOnWrite:  map[string]bool{},
		getsUntilGone: map[string]int{},
		charges:       map[chargeKey]memCharge{},
	}
}

// Seed inserts a record verbatim (fixed ID/version), for tests that address a
// known project without going through CreateProject.
func (f *MemStore) Seed(rec protocol.ProjectRecord) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.projects[rec.ID] = rec
}

// SweepOnWrite makes SetFile(id) report the row gone while reads still see it.
func (f *MemStore) SweepOnWrite(id string) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.sweptOnWrite[id] = true
}

// GoneAfterGets lets project reads of id succeed n more times, then 404.
func (f *MemStore) GoneAfterGets(id string, n int) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.getsUntilGone[id] = n
}

// SetFileCalls reports how many times SetFile ran.
func (f *MemStore) SetFileCalls() int {
	f.mu.Lock()
	defer f.mu.Unlock()
	return f.setFileCalls
}

// Project returns a snapshot of one stored record.
func (f *MemStore) Project(id string) (protocol.ProjectRecord, bool) {
	f.mu.Lock()
	defer f.mu.Unlock()
	p, ok := f.projects[id]
	return p, ok
}

func (f *MemStore) CreateProject(_ context.Context, owner string, req protocol.CreateProjectRequest) (protocol.ProjectRecord, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.seq++
	id := "p_t" + strconv.FormatInt(int64(f.seq), 36) + "_a"
	rec := protocol.ProjectRecord{
		ID: id, Name: req.Name, CreatedAt: 100, UpdatedAt: 100 + int64(f.seq),
		ExpiresAt: req.ExpiresAt,
		Source:    req.Source, Resource: req.Resource, Color: req.Color,
		ImageW: req.ImageW, ImageH: req.ImageH, HasImage: req.HasImage,
		Layout: req.Layout, OwnerSession: owner,
	}
	if rec.Name == "" {
		rec.Name = "Untitled"
	}
	f.projects[id] = rec
	return meta(rec), nil
}

func (f *MemStore) UpdateProject(_ context.Context, id string, patch store.ProjectPatch, expected int64) (protocol.ProjectRecord, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	p, ok := f.projects[id]
	if !ok {
		return protocol.ProjectRecord{}, store.ErrNotFound
	}
	if p.Version != expected {
		return protocol.ProjectRecord{}, store.ErrConflict
	}
	if patch.Name != nil {
		p.Name = *patch.Name
	}
	if patch.Color != nil {
		p.Color = *patch.Color
	}
	if patch.ExpiresAt != nil {
		p.ExpiresAt = *patch.ExpiresAt
	}
	if len(patch.Layout) > 0 {
		p.Layout = patch.Layout
	}
	p.Version++
	f.projects[id] = p
	return meta(p), nil
}

func (f *MemStore) SetFile(_ context.Context, id string, file store.StoredFile) (protocol.ProjectRecord, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.setFileCalls++
	p, ok := f.projects[id]
	if !ok || f.sweptOnWrite[id] {
		return protocol.ProjectRecord{}, store.ErrNotFound
	}
	if file.Kind == protocol.KindOriginal {
		p.OriginalPath = file.Path
		p.HasImage = true
		p.ImageW, p.ImageH = file.W, file.H
		p.OriginalHash = file.Hash
	} else {
		p.ResultPath = file.Path
	}
	p.Version++
	f.projects[id] = p
	return meta(p), nil
}

func (f *MemStore) DeleteProject(_ context.Context, id string) error {
	f.mu.Lock()
	defer f.mu.Unlock()
	delete(f.projects, id)
	f.dropCharges(id)
	return nil
}

// DeleteExpiredProjects mirrors the sweep query: expires_at in (0, now], never an id in keep.
func (f *MemStore) DeleteExpiredProjects(_ context.Context, now int64, limit int, keep []string) ([]string, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	kept := map[string]bool{}
	for _, id := range keep {
		kept[id] = true
	}
	ids := make([]string, 0)
	for id, p := range f.projects {
		if p.ExpiresAt > 0 && p.ExpiresAt <= now && !kept[id] && (limit <= 0 || len(ids) < limit) {
			ids = append(ids, id)
			delete(f.projects, id)
			f.dropCharges(id)
		}
	}
	return ids, nil
}
