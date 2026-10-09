package filestore

import (
	"io"
	"log"
	"os"
	"path/filepath"
	"strings"
)

// tmpPrefix names an upload's temp file until its rename commits it.
const tmpPrefix = ".tmp-"

// Charge is who one upload answers to: Peers, every project id its owner holds (asked only under a per-owner
// quota); Admit, when set, runs the commit inside the writer's STORAGE_QUOTA_PER_SESSION_BYTES check.
type Charge struct {
	Peers func() ([]string, error)
	Admit func(n int64, commit func() error) error
}

// PutStreamAs streams (id, kind)'s bytes into place at once and returns the store-relative path; a kind
// whose row must name the bytes first stages them with StageStreamAs instead.
func (s *Store) PutStreamAs(id, kind, ext string, r io.Reader, c Charge) (string, error) {
	st, err := s.StageStreamAs(id, kind, ext, r, c)
	if err != nil {
		return "", err
	}
	if err := st.Commit(); err != nil {
		if derr := st.Discard(); derr != nil {
			log.Printf("filestore: staged %s file of %s: %v", kind, id, derr)
		}
		return "", err
	}
	return st.Rel(), nil
}

// StageStreamAs streams (id, kind)'s bytes to disk under the quotas and the charge, durable (fsync first),
// but under a staged name no kind lookup resolves: the kind's current bytes stay served until Commit.
func (s *Store) StageStreamAs(id, kind, ext string, r io.Reader, c Charge) (Staged, error) {
	full, err := s.safeJoin(id, kind, ext)
	if err != nil {
		return nil, err
	}
	rel, err := filepath.Rel(s.root, full)
	if err != nil {
		return nil, err
	}
	dir := filepath.Dir(full)
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return nil, err
	}
	if err := s.guardSymlinkEscape(full); err != nil {
		return nil, err
	}
	tmp, err := os.CreateTemp(dir, tmpPrefix+"*")
	if err != nil {
		return nil, err
	}
	tmpName := tmp.Name()
	defer func() { // a no-op after the rename; anything left is the reconcile pass's
		if err := removeIfPresent(tmpName); err != nil {
			log.Printf("filestore: temp file %s: %v", tmpName, err)
		}
	}()
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
		return nil, err
	}
	// Only the committed store is capped: a temp file that then fails the check is removed above. The
	// owner's directories are a database read, taken before the ledger's transaction and the meter's lock.
	owned, err := s.usage.ownedDirs(s.peerDirs(c.Peers))
	if err != nil {
		return nil, err
	}
	stagedName, err := stagedPath(full)
	if err != nil {
		return nil, err
	}
	commit := func() error {
		return s.usage.commit(dir, kind, n, owned, func() error { return os.Rename(tmpName, stagedName) })
	}
	if c.Admit != nil {
		err = c.Admit(n, commit)
	} else {
		err = commit()
	}
	if err != nil {
		return nil, err
	}
	syncDir(dir) // best effort: makes the rename itself survive a crash
	return &staged{s: s, dir: dir, kind: kind, full: full, path: stagedName, rel: filepath.ToSlash(rel)}, nil
}

// peerDirs turns peers' project ids into their directories; an id that names none is skipped.
func (s *Store) peerDirs(peers func() ([]string, error)) func() ([]string, error) {
	if peers == nil {
		return nil
	}
	return func() ([]string, error) {
		ids, err := peers()
		if err != nil {
			return nil, err
		}
		dirs := make([]string, 0, len(ids))
		for _, id := range ids {
			if dir, err := s.projectDir(id); err == nil {
				dirs = append(dirs, dir)
			}
		}
		return dirs, nil
	}
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

// dropStaleSiblings removes same-kind files an earlier upload left under another extension, so a kind
// lookup never resolves to stale bytes; a staged upload is another's, left to it. Best effort: logged.
func dropStaleSiblings(dir, kind, keep string) {
	entries, err := os.ReadDir(dir)
	if err != nil {
		return
	}
	for _, e := range entries {
		if name := e.Name(); name != keep && strings.HasPrefix(name, kind+".") && !isStaged(name) {
			if err := removeIfPresent(filepath.Join(dir, name)); err != nil {
				log.Printf("filestore: stale %s file %s: %v", kind, name, err)
			}
		}
	}
}
