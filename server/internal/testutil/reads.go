package testutil

import (
	"context"
	"sort"

	"stencil/server/internal/protocol"
	"stencil/server/internal/store"
)

// The read half of MemStore, one method per column set the real store reads.

// ListProjects mirrors the real store: (updatedAt DESC, id DESC) order, keyset
// paging off page.After, no layout on a list row.
func (f *MemStore) ListProjects(_ context.Context, page store.ProjectPage) ([]protocol.ProjectRecord, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	out := make([]protocol.ProjectRecord, 0)
	for _, p := range f.projects {
		out = append(out, meta(p))
	}
	sort.Slice(out, func(i, j int) bool {
		if out[i].UpdatedAt != out[j].UpdatedAt {
			return out[i].UpdatedAt > out[j].UpdatedAt
		}
		return out[i].ID > out[j].ID
	})
	if after := page.After; after.ID != "" {
		for len(out) > 0 && !(out[0].UpdatedAt < after.UpdatedAt ||
			(out[0].UpdatedAt == after.UpdatedAt && out[0].ID < after.ID)) {
			out = out[1:]
		}
	}
	if page.Limit > 0 && len(out) > page.Limit {
		out = out[:page.Limit]
	}
	return out, nil
}

// GetProject is the full row, layout included (GET /projects/{id}).
func (f *MemStore) GetProject(_ context.Context, id string) (protocol.ProjectRecord, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	return f.read(id)
}

// GetProjectMeta mirrors the metadata-only read.
func (f *MemStore) GetProjectMeta(_ context.Context, id string) (protocol.ProjectRecord, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	p, err := f.read(id)
	return meta(p), err
}

func (f *MemStore) ProjectExists(_ context.Context, id string) (bool, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	_, err := f.read(id)
	return err == nil, nil
}

// OwnerProjectIDs mirrors the owner query: every project sharing id's owner, none when it has no owner.
func (f *MemStore) OwnerProjectIDs(_ context.Context, id string) ([]string, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	owner := f.projects[id].OwnerSession
	var ids []string
	for pid, p := range f.projects {
		if owner != "" && p.OwnerSession == owner {
			ids = append(ids, pid)
		}
	}
	return ids, nil
}

// read is every lookup's path, so GoneAfterGets counts them all. Callers hold f.mu.
func (f *MemStore) read(id string) (protocol.ProjectRecord, error) {
	f.reads++
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

// meta drops the layout, as every store write's RETURNING does.
func meta(p protocol.ProjectRecord) protocol.ProjectRecord {
	p.Layout = nil
	return p
}

// Reads reports how many project lookups of any kind have run.
func (f *MemStore) Reads() int {
	f.mu.Lock()
	defer f.mu.Unlock()
	return f.reads
}
