package filestore

// A staged upload: its bytes are on disk and in the quotas, under "<kind>.<ext>.staged-<hex>" — a name
// whose extension is not the kind's, so FindByKind and RemoveKind never resolve it — until its record is
// written. Only then does Commit rename it into place; a refused record Discards it, leaving the old bytes.

import (
	"crypto/rand"
	"encoding/hex"
	"os"
	"path/filepath"
	"strings"
)

// stagedInfix marks a staged upload's name; the reconcile pass drops one a crash left past tmpMaxAge.
const stagedInfix = ".staged-"

// Staged is one upload waiting for its record. Commit or Discard runs once.
type Staged interface {
	Rel() string    // the store-relative path the bytes take on Commit, for the row
	Commit() error  // move into place and drop the kind's other extensions
	Discard() error // remove the staged bytes, crediting the meter
}

type staged struct {
	s                     *Store
	dir, kind, full, path string
	rel                   string
}

func (x *staged) Rel() string { return x.rel }

func (x *staged) Commit() error {
	if err := x.s.guardSymlinkEscape(x.full); err != nil {
		return err
	}
	err := x.s.usage.free(func() int64 { return kindBytes(x.dir, x.kind) }, func() error {
		if err := os.Rename(x.path, x.full); err != nil {
			return err
		}
		dropStaleSiblings(x.dir, x.kind, filepath.Base(x.full))
		return nil
	})
	if err == nil {
		syncDir(x.dir)
	}
	return err
}

func (x *staged) Discard() error {
	return x.s.usage.free(func() int64 { return kindBytes(x.dir, x.kind) }, func() error { return removeIfPresent(x.path) })
}

// stagedPath names a fresh staged file beside full; the random part keeps concurrent uploads apart.
func stagedPath(full string) (string, error) {
	var b [8]byte
	if _, err := rand.Read(b[:]); err != nil {
		return "", err
	}
	return full + stagedInfix + hex.EncodeToString(b[:]), nil
}

func isStaged(name string) bool { return strings.Contains(name, stagedInfix) }
