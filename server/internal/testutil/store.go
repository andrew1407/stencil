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

// MemStore is an in-memory project + session store. It satisfies
// httpapi.ProjectStore, httpapi.SessionStore, and hub.Store.
type MemStore struct {
	mu       sync.Mutex
	projects map[string]protocol.ProjectRecord
	sessions map[string]auth.Session
	seq      int
	// setFileCalls counts SetFile invocations, so tests can assert that
	// filestore-only kinds (video/variantN) never touch the project record.
	setFileCalls int
	// sweptOnWrite[id]=true makes SetFile report the row gone (ErrNotFound) even
	// though GetProject still sees it — simulating the expiry sweep deleting the
	// project between the upload handler's existence check and its SetFile write.
	sweptOnWrite map[string]bool
	// getsUntilGone[id]=N lets GetProject succeed N more times, then report the
	// row gone — simulating the sweep firing between the upload handler's
	// pre-check and its post-write re-check for filestore-only kinds.
	getsUntilGone map[string]int
}

// NewMemStore returns an empty in-memory store.
func NewMemStore() *MemStore {
	return &MemStore{
		projects:      map[string]protocol.ProjectRecord{},
		sessions:      map[string]auth.Session{},
		sweptOnWrite:  map[string]bool{},
		getsUntilGone: map[string]int{},
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

// GoneAfterGets lets GetProject(id) succeed n more times, then 404.
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

func (f *MemStore) ResolveToken(_ context.Context, hash []byte) (auth.Session, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	if s, ok := f.sessions[string(hash)]; ok {
		return s, nil
	}
	return auth.Session{}, auth.ErrInvalidToken
}

func (f *MemStore) CreateSession(_ context.Context, hash []byte, label string, createdAt, expiresAt int64) (auth.Session, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.seq++
	s := auth.Session{ID: "s_" + strconv.Itoa(f.seq), Label: label, CreatedAt: createdAt, ExpiresAt: expiresAt}
	f.sessions[string(hash)] = s
	return s, nil
}

func (f *MemStore) ListProjects(context.Context) ([]protocol.ProjectRecord, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	out := []protocol.ProjectRecord{}
	for _, p := range f.projects {
		p.Layout = nil
		out = append(out, p)
	}
	return out, nil
}

func (f *MemStore) GetProject(_ context.Context, id string) (protocol.ProjectRecord, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	if n, ok := f.getsUntilGone[id]; ok {
		if n <= 0 {
			return protocol.ProjectRecord{}, store.ErrNotFound
		}
		f.getsUntilGone[id] = n - 1
	}
	if p, ok := f.projects[id]; ok {
		return p, nil
	}
	return protocol.ProjectRecord{}, store.ErrNotFound
}

func (f *MemStore) CreateProject(_ context.Context, owner string, req protocol.CreateProjectRequest) (protocol.ProjectRecord, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.seq++
	id := "p_t" + strconv.FormatInt(int64(f.seq), 36) + "_a"
	rec := protocol.ProjectRecord{
		ID: id, Name: req.Name, CreatedAt: 100, UpdatedAt: 100 + int64(f.seq),
		ExpiresAt: req.ExpiresAt,
		Source:    req.Source, Resource: req.Resource, Color: req.Color, OriginalContent: req.OriginalContent,
		Layout: req.Layout, OwnerSession: owner,
	}
	if rec.Name == "" {
		rec.Name = "Untitled"
	}
	f.projects[id] = rec
	return rec, nil
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
	return p, nil
}

func (f *MemStore) SetFile(_ context.Context, id, kind, rel string, w, h int) (protocol.ProjectRecord, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.setFileCalls++
	p, ok := f.projects[id]
	if !ok || f.sweptOnWrite[id] {
		return protocol.ProjectRecord{}, store.ErrNotFound
	}
	if kind == protocol.KindOriginal {
		p.OriginalPath = rel
		p.HasImage = true
		p.ImageW, p.ImageH = w, h
	} else {
		p.ResultPath = rel
	}
	p.Version++
	f.projects[id] = p
	return p, nil
}

func (f *MemStore) DeleteProject(_ context.Context, id string) error {
	f.mu.Lock()
	defer f.mu.Unlock()
	delete(f.projects, id)
	return nil
}
