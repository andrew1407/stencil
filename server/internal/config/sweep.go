// Background-sweep tunables: the expiry sweep's batch and worker pool, and the filestore reconcile pass
// that clears bytes no project row owns and temp files a crashed upload left behind.
package config

import "time"

// SweepOptions sizes the periodic maintenance passes.
type SweepOptions struct {
	Batch     int           // SWEEP_BATCH: rows one DELETE ... RETURNING takes
	Workers   int           // SWEEP_WORKERS: concurrent per-project byte drops
	Reconcile time.Duration // FILESTORE_RECONCILE_MINUTES: reconcile cadence; 0 disables it
	TmpMaxAge time.Duration // FILESTORE_TMP_MAX_AGE_MINUTES: an upload temp file older than this is dead
}

const (
	defaultSweepBatch   = 500
	defaultSweepWorkers = 8
	defaultReconcile    = 6 * time.Hour
	// Far past the longest upload (the HTTP read timeout), so a live upload's temp file is never taken.
	defaultTmpMaxAge = time.Hour
)

func loadSweep(get getter, cfg *Config) error {
	s := &cfg.Sweep
	var err error
	if s.Batch, err = positiveInt(get, "SWEEP_BATCH", defaultSweepBatch, 1); err != nil {
		return err
	}
	if s.Workers, err = positiveInt(get, "SWEEP_WORKERS", defaultSweepWorkers, 1); err != nil {
		return err
	}
	if s.Reconcile, err = duration(get, "FILESTORE_RECONCILE_MINUTES", defaultReconcile, time.Minute, 0); err != nil {
		return err
	}
	s.TmpMaxAge, err = duration(get, "FILESTORE_TMP_MAX_AGE_MINUTES", defaultTmpMaxAge, time.Minute, 1)
	return err
}
