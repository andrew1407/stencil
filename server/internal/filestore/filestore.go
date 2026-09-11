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

// ErrNotFound is returned when a requested file does not exist.
var ErrNotFound = errors.New("filestore: not found")

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

// NewWithQuota is New plus an aggregate storage cap in bytes (0 = unlimited).
func NewWithQuota(root string, quota int64) (*Store, error) {
	s, err := New(root)
	if err != nil {
		return nil, err
	}
	if quota > 0 {
		if s.usage, err = newCapped(s.root, quota); err != nil {
			return nil, err
		}
	}
	return s, nil
}

func (s *Store) Root() string { return s.root }

// Get returns the bytes for (id, kind, ext). Missing files yield ErrNotFound.
func (s *Store) Get(id, kind, ext string) ([]byte, error) {
	full, err := s.safeJoin(id, kind, ext)
	if err != nil {
		return nil, err
	}
	if err := s.guardSymlinkEscape(full); err != nil {
		return nil, err
	}
	data, err := os.ReadFile(full)
	if errors.Is(err, os.ErrNotExist) {
		return nil, ErrNotFound
	}
	return data, err
}

// GetByRelPath returns the bytes at a previously recorded store-relative path
// (e.g. ProjectRecord.OriginalPath). The path is re-confined before reading.
func (s *Store) GetByRelPath(rel string) ([]byte, error) {
	full, err := s.confine(filepath.FromSlash(rel))
	if err != nil {
		return nil, err
	}
	if err := s.guardSymlinkEscape(full); err != nil {
		return nil, err
	}
	data, err := os.ReadFile(full)
	if errors.Is(err, os.ErrNotExist) {
		return nil, ErrNotFound
	}
	return data, err
}

// FindByKind returns the store-relative path of the file held for (id, kind),
// whatever its extension. It owns the "<kind>.<ext>" naming invariant together
// with Put (whose stale-extension cleanup guarantees at most one file per
// kind). Missing files yield ErrNotFound.
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

// OpenByRelPath opens the file at a previously recorded store-relative path for
// streaming reads (e.g. http.ServeContent). The path is re-confined before
// opening; the caller owns (and must close) the returned file.
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

// RemoveKind deletes the single file held for (id, kind), whatever its
// extension. Removing a kind with no stored bytes is not an error (the delete
// is idempotent, per llm-contract.md §9).
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
	var size int64
	if fi, err := os.Stat(full); err == nil {
		size = fi.Size()
	}
	if err := os.Remove(full); err != nil && !errors.Is(err, os.ErrNotExist) {
		return err
	}
	s.usage.release(size)
	return nil
}

// Remove deletes the entire directory for a project. Removing a non-existent
// project is not an error.
func (s *Store) Remove(id string) error {
	dir, err := s.projectDir(id)
	if err != nil {
		return err
	}
	size := s.usage.dirBytes(dir)
	if err := os.RemoveAll(dir); err != nil && !errors.Is(err, os.ErrNotExist) {
		return err
	}
	s.usage.release(size)
	return nil
}

// List returns the store-relative paths of every file held for a project.
func (s *Store) List(id string) ([]string, error) {
	dir, err := s.projectDir(id)
	if err != nil {
		return nil, err
	}
	entries, err := os.ReadDir(dir)
	if errors.Is(err, os.ErrNotExist) {
		return nil, nil
	}
	if err != nil {
		return nil, err
	}
	out := make([]string, 0, len(entries))
	for _, e := range entries {
		if e.IsDir() {
			continue
		}
		rel, err := filepath.Rel(s.root, filepath.Join(dir, e.Name()))
		if err != nil {
			return nil, err
		}
		out = append(out, filepath.ToSlash(rel))
	}
	return out, nil
}

// guardSymlinkEscape ensures that, if any existing ancestor of full is a
// symlink, the resolved real path still lies within the store root. This closes
// the symlink-swap traversal gap that a pure string prefix check would miss.
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
