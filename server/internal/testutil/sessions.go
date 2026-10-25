package testutil

import (
	"context"
	"strconv"

	"stencil/server/internal/auth"
	"stencil/server/internal/store"
)

// The session half of MemStore: httpapi.SessionStore and auth.SessionResolver.

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

func (f *MemStore) DeleteSession(_ context.Context, id string) error {
	f.mu.Lock()
	defer f.mu.Unlock()
	for hash, s := range f.sessions {
		if s.ID == id {
			delete(f.sessions, hash)
			return nil
		}
	}
	return store.ErrNotFound
}

func (f *MemStore) DeleteAllSessions(_ context.Context) (int, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	n := len(f.sessions)
	clear(f.sessions)
	return n, nil
}
