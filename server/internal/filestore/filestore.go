// Package filestore is a small, secured file store for project image bytes. It
// is deliberately custom (no object-store dependency): files live under a single
// root, addressed only by a validated project id plus a fixed kind/extension, so
// no client-supplied filename ever reaches disk. Every path flows through
// safeJoin (see path.go), and writes are atomic (temp file + rename).
package filestore

import (
	"errors"
	"io/fs"
	"os"
	"path/filepath"
	"strings"
	"sync"
)

// ErrNotFound is returned when a requested file does not exist.
var ErrNotFound = errors.New("filestore: not found")

// ErrQuotaExceeded is returned by Put when a write would push the aggregate
// stored bytes past the configured quota.
var ErrQuotaExceeded = errors.New("filestore: storage quota exceeded")

// Store is a root-confined file store.
type Store struct {
	root  string
	quota int64      // aggregate byte cap; 0 = unlimited (usage untracked)
	qmu   sync.Mutex // guards usage
	usage int64      // total bytes under root, maintained only when quota > 0
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

// NewWithQuota is New plus an aggregate storage cap in bytes (0 = unlimited).
// The starting usage is computed by walking the root once; Put/RemoveKind/
// Remove keep the counter current after that.
func NewWithQuota(root string, quota int64) (*Store, error) {
	s, err := New(root)
	if err != nil {
		return nil, err
	}
	if quota > 0 {
		s.quota = quota
		if s.usage, err = dirSize(s.root); err != nil {
			return nil, err
		}
	}
	return s, nil
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
	// Quota check + accounting up front, so two concurrent Puts cannot both
	// squeeze past the cap; released again on any failure below. Existing bytes
	// for this kind (any extension) are replaced, so they credit the delta.
	delta := int64(len(data)) - s.kindBytes(filepath.Dir(full), kind)
	if err := s.reserve(delta); err != nil {
		return "", err
	}
	tmp, err := os.CreateTemp(filepath.Dir(full), ".tmp-*")
	if err != nil {
		s.release(delta)
		return "", err
	}
	tmpName := tmp.Name()
	defer os.Remove(tmpName) // no-op after a successful rename
	if _, err := tmp.Write(data); err != nil {
		tmp.Close()
		s.release(delta)
		return "", err
	}
	if err := tmp.Close(); err != nil {
		s.release(delta)
		return "", err
	}
	if err := os.Rename(tmpName, full); err != nil {
		s.release(delta)
		return "", err
	}
	// Best-effort: drop same-kind files left by an earlier upload with a
	// different extension, so kind-based lookups never resolve to stale bytes.
	// Their bytes were already credited into the reserve delta above.
	if entries, err := os.ReadDir(filepath.Dir(full)); err == nil {
		for _, e := range entries {
			name := e.Name()
			if name != filepath.Base(full) && strings.HasPrefix(name, kind+".") {
				_ = os.Remove(filepath.Join(filepath.Dir(full), name))
			}
		}
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
	s.release(size)
	return nil
}

// Remove deletes the entire directory for a project. Removing a non-existent
// project is not an error.
func (s *Store) Remove(id string) error {
	dir, err := s.projectDir(id)
	if err != nil {
		return err
	}
	var size int64
	if s.quota > 0 {
		size, _ = dirSize(dir) // 0 when the directory does not exist
	}
	if err := os.RemoveAll(dir); err != nil && !errors.Is(err, os.ErrNotExist) {
		return err
	}
	s.release(size)
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

// kindBytes sums the bytes currently held for kind (any extension) in dir;
// 0 when quota accounting is off.
func (s *Store) kindBytes(dir, kind string) int64 {
	if s.quota <= 0 {
		return 0
	}
	var n int64
	if entries, err := os.ReadDir(dir); err == nil {
		for _, e := range entries {
			if strings.HasPrefix(e.Name(), kind+".") {
				if info, err := e.Info(); err == nil {
					n += info.Size()
				}
			}
		}
	}
	return n
}

// reserve accounts a byte delta about to land on disk, failing with
// ErrQuotaExceeded when the aggregate cap would be passed. Hand the delta back
// to release if the write later fails.
func (s *Store) reserve(delta int64) error {
	if s.quota <= 0 {
		return nil
	}
	s.qmu.Lock()
	defer s.qmu.Unlock()
	if s.usage+delta > s.quota {
		return ErrQuotaExceeded
	}
	s.usage += delta
	return nil
}

// release gives delta bytes back to the quota accounting (no-op when
// untracked or zero).
func (s *Store) release(delta int64) {
	if s.quota <= 0 || delta == 0 {
		return
	}
	s.qmu.Lock()
	s.usage -= delta
	s.qmu.Unlock()
}

// dirSize sums the sizes of every regular file under dir.
func dirSize(dir string) (int64, error) {
	var n int64
	err := filepath.WalkDir(dir, func(_ string, d fs.DirEntry, err error) error {
		if err != nil || d.IsDir() {
			return err
		}
		info, err := d.Info()
		if err != nil {
			return err
		}
		n += info.Size()
		return nil
	})
	return n, err
}
