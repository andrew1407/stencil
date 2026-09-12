package filestore

import (
	"bytes"
	"io"
	"os"
	"path/filepath"
	"strings"
)

// Put writes bytes for (id, kind) with the given extension, atomically. It
// returns the store-relative path recorded in project metadata.
func (s *Store) Put(id, kind, ext string, data []byte) (string, error) {
	return s.PutStream(id, kind, ext, bytes.NewReader(data))
}

// PutStream is Put over a reader: the bytes go straight to the temp file instead
// of being buffered whole in memory first, matching how downloads already stream.
// The write is atomic (temp + rename) and durable — the temp file is fsynced
// before the rename, so a crash cannot leave a renamed but empty file.
func (s *Store) PutStream(id, kind, ext string, r io.Reader) (string, error) {
	full, err := s.safeJoin(id, kind, ext)
	if err != nil {
		return "", err
	}
	dir := filepath.Dir(full)
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return "", err
	}
	if err := s.guardSymlinkEscape(full); err != nil {
		return "", err
	}
	tmp, err := os.CreateTemp(dir, ".tmp-*")
	if err != nil {
		return "", err
	}
	tmpName := tmp.Name()
	defer os.Remove(tmpName) // no-op after a successful rename
	n, err := io.Copy(tmp, r)
	if err == nil && n == 0 {
		err = ErrEmpty
	}
	if err == nil {
		err = tmp.Sync() // the bytes, before the name points at them
	}
	if cerr := tmp.Close(); err == nil {
		err = cerr
	}
	if err != nil {
		return "", err
	}
	// Only the committed store is capped: a temp file that then fails the check
	// is removed above.
	delta, err := s.usage.reserveReplacing(dir, kind, n)
	if err != nil {
		return "", err
	}
	if err := os.Rename(tmpName, full); err != nil {
		s.usage.release(delta)
		return "", err
	}
	syncDir(dir) // best effort: makes the rename itself survive a crash
	// Best-effort: drop same-kind files left by an earlier upload with a
	// different extension, so kind-based lookups never resolve to stale bytes.
	// Their bytes were already credited into the reserve delta above.
	if entries, err := os.ReadDir(dir); err == nil {
		for _, e := range entries {
			name := e.Name()
			if name != filepath.Base(full) && strings.HasPrefix(name, kind+".") {
				_ = os.Remove(filepath.Join(dir, name))
			}
		}
	}
	rel, err := filepath.Rel(s.root, full)
	if err != nil {
		return "", err
	}
	return filepath.ToSlash(rel), nil
}

// syncDir flushes a directory entry. Not every filesystem allows it, and by here
// the bytes are already committed, so a failure is not the caller's problem.
func syncDir(dir string) {
	d, err := os.Open(dir)
	if err != nil {
		return
	}
	_ = d.Sync()
	_ = d.Close()
}
