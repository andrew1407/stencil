// Package filestore is a small, secured file store for project image bytes. It
// is deliberately custom (no object-store dependency): files live under a single
// root, addressed only by a validated project id plus a fixed kind/extension, so
// no client-supplied filename ever reaches disk. Every path flows through
// safeJoin (see path.go), and writes are atomic and durable (put.go).
package filestore

import (
	"errors"
	"os"
	"path/filepath"
	"strings"
)

// Store is a root-confined file store.
type Store struct {
	root  string
	usage usageMeter // storage-quota accounting (quota.go)
}

// New creates the store rooted at the given directory, resolving it to an
// absolute, symlink-free path and creating it if necessary.
func New(root string) (*Store, error) {
	if root == "" {
		return nil, errors.New("filestore: empty root")
	}
	abs, err := filepath.Abs(root)
	if err != nil {
		return nil, err
	}
	if err := os.MkdirAll(abs, 0o755); err != nil {
		return nil, err
	}
	// Resolve symlinks on the root so prefix checks compare real paths.
	if resolved, err := filepath.EvalSymlinks(abs); err == nil {
		abs = resolved
	}
	return &Store{root: filepath.Clean(abs), usage: unmetered{}}, nil
}

// Quotas caps stored bytes, in bytes: Total over the whole store, PerOwner over the projects one owner
// session holds (PutStreamAs's Charge names them). 0 = no cap.
type Quotas struct {
	Total    int64
	PerOwner int64
}

// NewWithQuotas is New plus both caps; with neither set it meters nothing.
func NewWithQuotas(root string, q Quotas) (*Store, error) {
	s, err := New(root)
	if err != nil {
		return nil, err
	}
	if q.Total > 0 || q.PerOwner > 0 {
		if s.usage, err = newCapped(s.root, q); err != nil {
			return nil, err
		}
	}
	return s, nil
}

func (s *Store) Root() string { return s.root }

// FindByKind returns the store-relative path held for (id, kind), whatever its extension; it owns the
// "<kind>.<ext>" naming invariant together with PutStreamAs. Missing files yield ErrNotFound.
func (s *Store) FindByKind(id, kind string) (string, error) {
	dir, err := s.projectDir(id)
	if err != nil {
		return "", err
	}
	entries, err := os.ReadDir(dir)
	if errors.Is(err, os.ErrNotExist) {
		return "", ErrNotFound
	}
	if err != nil {
		return "", err
	}
	for _, e := range entries {
		name := e.Name()
		if e.IsDir() || strings.TrimSuffix(name, filepath.Ext(name)) != kind {
			continue
		}
		rel, err := filepath.Rel(s.root, filepath.Join(dir, name))
		if err != nil {
			return "", err
		}
		return filepath.ToSlash(rel), nil
	}
	return "", ErrNotFound
}

// OpenByRelPath opens a recorded store-relative path for streaming reads (http.ServeContent). The path
// is re-confined before opening; the caller owns and must close the returned file.
func (s *Store) OpenByRelPath(rel string) (*os.File, error) {
	full, err := s.confine(filepath.FromSlash(rel))
	if err != nil {
		return nil, err
	}
	if err := s.guardSymlinkEscape(full); err != nil {
		return nil, err
	}
	f, err := os.Open(full)
	if errors.Is(err, os.ErrNotExist) {
		return nil, ErrNotFound
	}
	return f, err
}

// RemoveKind deletes the single file held for (id, kind), whatever its extension. Removing a kind with
// no stored bytes is not an error (idempotent, per llm-contract.md §9).
func (s *Store) RemoveKind(id, kind string) error {
	rel, err := s.FindByKind(id, kind)
	if errors.Is(err, ErrNotFound) {
		return nil
	}
	if err != nil {
		return err
	}
	full, err := s.confine(filepath.FromSlash(rel))
	if err != nil {
		return err
	}
	if err := s.guardSymlinkEscape(full); err != nil {
		return err
	}
	return s.usage.free(func() int64 { return fileSize(full) }, func() error { return removeIfPresent(full) })
}

// Remove deletes the entire directory for a project. Removing a non-existent
// project is not an error.
func (s *Store) Remove(id string) error {
	dir, err := s.projectDir(id)
	if err != nil {
		return err
	}
	return s.usage.free(func() int64 { n, _ := dirSize(dir); return n }, func() error {
		if err := os.RemoveAll(dir); err != nil && !errors.Is(err, os.ErrNotExist) {
			return err
		}
		return nil
	})
}

// guardSymlinkEscape ensures that if any existing ancestor of full is a symlink, the resolved real path
// still lies within the store root — the symlink-swap gap a string prefix check would miss.
func (s *Store) guardSymlinkEscape(full string) error {
	dir := filepath.Dir(full)
	resolved, err := filepath.EvalSymlinks(dir)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return nil // directory not created yet; Put will MkdirAll a real dir
		}
		return err
	}
	if _, err := s.confine(resolved); err != nil {
		return err
	}
	return nil
}

// fileSize is a regular file's size, 0 when it is gone.
func fileSize(path string) int64 {
	if fi, err := os.Stat(path); err == nil {
		return fi.Size()
	}
	return 0
}

// removeIfPresent deletes one file; one already gone is not an error.
func removeIfPresent(path string) error {
	if err := os.Remove(path); err != nil && !errors.Is(err, os.ErrNotExist) {
		return err
	}
	return nil
}
