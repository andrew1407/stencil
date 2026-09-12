package filestore

// Storage-quota accounting, kept apart from the byte moving: the store asks the
// meter what a write costs and hands bytes back when one is undone, and never
// touches a counter itself. STORAGE_QUOTA_BYTES=0 installs the no-op meter, so
// the unlimited case costs neither a lock nor a directory walk.

import (
	"io/fs"
	"os"
	"path/filepath"
	"strings"
	"sync"
)

// usageMeter accounts the aggregate bytes held under the store root.
type usageMeter interface {
	// reserveReplacing accounts n bytes about to land for (dir, kind), crediting
	// whatever bytes that write replaces, and returns the accounted delta. Hand
	// the delta back to release if the write then fails.
	reserveReplacing(dir, kind string, n int64) (int64, error)
	// release gives delta bytes back.
	release(delta int64)
	// dirBytes is what removing dir would free.
	dirBytes(dir string) int64
}

// unmetered is the quota-off meter: every write fits, nothing is counted.
type unmetered struct{}

func (unmetered) reserveReplacing(string, string, int64) (int64, error) { return 0, nil }
func (unmetered) release(int64)                                         {}
func (unmetered) dirBytes(string) int64                                 { return 0 }

// capped meters against an aggregate byte cap.
type capped struct {
	quota int64
	mu    sync.Mutex
	usage int64 // total bytes under root, guarded by mu
}

// newCapped starts the counter by walking the root once; Put/RemoveKind/Remove
// keep it current after that.
func newCapped(root string, quota int64) (*capped, error) {
	usage, err := dirSize(root)
	if err != nil {
		return nil, err
	}
	return &capped{quota: quota, usage: usage}, nil
}

// reserveReplacing takes the credit and the reservation under one lock, so two
// concurrent writes cannot both slip past the cap.
func (c *capped) reserveReplacing(dir, kind string, n int64) (int64, error) {
	delta := n - kindBytes(dir, kind)
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.usage+delta > c.quota {
		return 0, ErrQuotaExceeded
	}
	c.usage += delta
	return delta, nil
}

func (c *capped) release(delta int64) {
	if delta == 0 {
		return
	}
	c.mu.Lock()
	c.usage -= delta
	c.mu.Unlock()
}

func (c *capped) dirBytes(dir string) int64 {
	n, _ := dirSize(dir) // 0 when the directory does not exist
	return n
}

// kindBytes sums the bytes currently held for kind (any extension) in dir.
func kindBytes(dir, kind string) int64 {
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
