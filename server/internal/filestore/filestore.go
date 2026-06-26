// Package filestore is a small, secured file store for project image bytes. It
// is deliberately custom (no object-store dependency): files live under a single
// root, addressed only by a validated project id plus a fixed kind/extension, so
// no client-supplied filename ever reaches disk. Every path flows through
// safeJoin (see path.go), and writes are atomic (temp file + rename).
package filestore

import (
	"errors"
	"io"
	"os"
	"path/filepath"
)

// ErrNotFound is returned when a requested file does not exist.
var ErrNotFound = errors.New("filestore: not found")

// Store is a root-confined file store.
type Store struct {
	root string
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
	return &Store{root: filepath.Clean(abs)}, nil
}

// Root returns the absolute store root (mainly for tests/logging).
func (s *Store) Root() string { return s.root }

// Put writes bytes for (id, kind) with the given extension, atomically. It
// returns the store-relative path recorded in project metadata.
func (s *Store) Put(id, kind, ext string, data []byte) (string, error) {
	full, err := s.safeJoin(id, kind, ext)
	if err != nil {
		return "", err
	}
	if err := os.MkdirAll(filepath.Dir(full), 0o755); err != nil {
		return "", err
	}
	if err := s.guardSymlinkEscape(full); err != nil {
		return "", err
	}
	tmp, err := os.CreateTemp(filepath.Dir(full), ".tmp-*")
	if err != nil {
		return "", err
	}
	tmpName := tmp.Name()
	defer os.Remove(tmpName) // no-op after a successful rename
	if _, err := tmp.Write(data); err != nil {
		tmp.Close()
		return "", err
	}
	if err := tmp.Close(); err != nil {
		return "", err
	}
	if err := os.Rename(tmpName, full); err != nil {
		return "", err
	}
	rel, err := filepath.Rel(s.root, full)
	if err != nil {
		return "", err
	}
	return filepath.ToSlash(rel), nil
}

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

// Remove deletes the entire directory for a project. Removing a non-existent
// project is not an error.
func (s *Store) Remove(id string) error {
	dir, err := s.projectDir(id)
	if err != nil {
		return err
	}
	if err := os.RemoveAll(dir); err != nil && !errors.Is(err, os.ErrNotExist) {
		return err
	}
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

// Copy streams src into the store for (id, kind, ext); convenience over Put for
// io.Reader sources.
func (s *Store) Copy(id, kind, ext string, src io.Reader) (string, error) {
	data, err := io.ReadAll(src)
	if err != nil {
		return "", err
	}
	return s.Put(id, kind, ext, data)
}
