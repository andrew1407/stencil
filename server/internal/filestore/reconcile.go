package filestore

import (
	"context"
	"errors"
	"log"
	"os"
	"path/filepath"
	"strings"
	"time"

	"stencil/server/internal/validate"
)

// Reconciled counts what one Reconcile pass removed.
type Reconciled struct {
	Orphans int // project directories no row owns
	Temps   int // upload temp files past their age
}

// Reconcile removes what a crash or a failed removal left behind: upload temp files older than tmpMaxAge,
// and every project directory whose id owns reports has no row. A directory whose check fails is kept.
func (s *Store) Reconcile(ctx context.Context, owns func(ctx context.Context, id string) (bool, error), tmpMaxAge time.Duration) (Reconciled, error) {
	var got Reconciled
	entries, err := os.ReadDir(filepath.Join(s.root, "projects"))
	if errors.Is(err, os.ErrNotExist) {
		return got, nil
	}
	if err != nil {
		return got, err
	}
	cutoff := time.Now().Add(-tmpMaxAge)
	for _, e := range entries {
		if err := ctx.Err(); err != nil {
			return got, err
		}
		id := e.Name()
		if !e.IsDir() || !validate.ProjectID(id) {
			continue // not ours to judge: the store only ever writes id-shaped directories
		}
		dir, err := s.projectDir(id)
		if err != nil {
			continue
		}
		got.Temps += s.dropStaleTemps(dir, cutoff)
		owned, err := owns(ctx, id)
		if err != nil {
			log.Printf("filestore: reconcile %s: %v", id, err)
			continue
		}
		if !owned {
			if err := s.Remove(id); err != nil {
				log.Printf("filestore: reconcile %s: %v", id, err)
				continue
			}
			got.Orphans++
		}
	}
	return got, nil
}

// dropStaleTemps removes dir's upload temp files last written before cutoff; a live upload's is newer.
func (s *Store) dropStaleTemps(dir string, cutoff time.Time) int {
	entries, err := os.ReadDir(dir)
	if err != nil {
		return 0
	}
	n := 0
	for _, e := range entries {
		info, err := e.Info()
		if err != nil || !strings.HasPrefix(e.Name(), tmpPrefix) || !info.ModTime().Before(cutoff) {
			continue
		}
		tmp := filepath.Join(dir, e.Name())
		if err := s.usage.free(func() int64 { return fileSize(tmp) }, func() error { return removeIfPresent(tmp) }); err != nil {
			log.Printf("filestore: stale temp %s: %v", tmp, err)
			continue
		}
		n++
	}
	return n
}
