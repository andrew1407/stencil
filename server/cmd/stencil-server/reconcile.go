package main

import (
	"context"
	"log"
	"sync"
	"time"

	"stencil/server/internal/filestore"
)

// startReconcile runs the filestore reconcile pass at boot and every interval (0 disables it): bytes no
// project row owns, and upload temp files older than tmpMaxAge, left by a crash or a failed removal.
func startReconcile(ctx context.Context, wg *sync.WaitGroup, fs *filestore.Store, rows projectExister, opTimeout, interval, tmpMaxAge time.Duration) {
	if interval <= 0 {
		log.Printf("filestore reconcile disabled (FILESTORE_RECONCILE_MINUTES=0)")
		return
	}
	owns := func(ctx context.Context, id string) (bool, error) {
		ctx, cancel := context.WithTimeout(ctx, opTimeout)
		defer cancel()
		return rows.ProjectExists(ctx, id)
	}
	every(ctx, wg, interval, func(ctx context.Context) {
		got, err := fs.Reconcile(ctx, owns, tmpMaxAge)
		if err != nil {
			log.Printf("filestore reconcile: %v", err)
		}
		if got.Orphans+got.Temps > 0 {
			log.Printf("filestore reconcile: removed %d orphaned project dir(s), %d stale temp file(s)", got.Orphans, got.Temps)
		}
	})
}
