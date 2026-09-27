package filestore

// Storage-quota accounting, kept apart from the byte moving: the store hands the meter each filesystem
// change together with how to measure it, and never touches a counter itself. With both quotas 0 the
// no-op meter is installed, so the unlimited case costs neither a lock nor a directory walk.

import (
	"io/fs"
	"os"
	"path/filepath"
	"strings"
	"sync"
)

// usageMeter accounts the aggregate bytes held under the store root. A capped meter runs each change
// under its lock, measuring before and after, so two writes of one kind cannot both credit the old bytes.
type usageMeter interface {
	// ownedDirs asks owned for the owner's directories only when a per-owner quota needs them.
	ownedDirs(owned func() ([]string, error)) ([]string, error)
	// commit runs swap, which replaces whatever (dir, kind) held with n new bytes; refused past a quota.
	// owned lists the directories of every project dir's owner holds (nil = no owner).
	commit(dir, kind string, n int64, owned []string, swap func() error) error
	// free runs remove, crediting what measure reports it freed.
	free(measure func() int64, remove func() error) error
}

// unmetered is the quota-off meter: every write fits, nothing is counted.
type unmetered struct{}

func (unmetered) ownedDirs(func() ([]string, error)) ([]string, error) { return nil, nil }
func (unmetered) commit(_, _ string, _ int64, _ []string, swap func() error) error {
	return swap()
}
func (unmetered) free(_ func() int64, remove func() error) error { return remove() }

// capped meters against the aggregate cap, one owner's cap, or both; a zero quota is no cap.
type capped struct {
	quota    int64
	perOwner int64
	mu       sync.Mutex
	usage    int64 // total bytes under root, guarded by mu; kept only under an aggregate quota
}

// newCapped starts the counter by walking the root once; commit/free keep it current after that.
func newCapped(root string, q Quotas) (*capped, error) {
	c := &capped{quota: q.Total, perOwner: q.PerOwner}
	if q.Total > 0 {
		usage, err := dirSize(root)
		if err != nil {
			return nil, err
		}
		c.usage = usage
	}
	return c, nil
}

func (c *capped) ownedDirs(owned func() ([]string, error)) ([]string, error) {
	if c.perOwner <= 0 || owned == nil {
		return nil, nil
	}
	return owned()
}

func (c *capped) commit(dir, kind string, n int64, dirs []string, swap func() error) error {
	c.mu.Lock()
	defer c.mu.Unlock()
	before := kindBytes(dir, kind)
	if c.quota > 0 && c.usage+n-before > c.quota {
		return ErrQuotaExceeded
	}
	if len(dirs) > 0 && ownedBytes(dir, dirs)+n-before > c.perOwner {
		return ErrQuotaExceeded
	}
	if err := swap(); err != nil {
		return err
	}
	c.usage += kindBytes(dir, kind) - before // exact even when a stale sibling could not be removed
	return nil
}

func (c *capped) free(measure func() int64, remove func() error) error {
	c.mu.Lock()
	defer c.mu.Unlock()
	before := measure()
	err := remove()
	c.usage -= before - measure()
	return err
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

// ownedBytes sums the committed bytes of dir and every directory in dirs, each once; an upload's temp
// file is not yet stored, so it counts against nobody.
func ownedBytes(dir string, dirs []string) int64 {
	seen := map[string]bool{}
	var n int64
	for _, d := range append([]string{dir}, dirs...) {
		if seen[d] {
			continue
		}
		seen[d] = true
		if entries, err := os.ReadDir(d); err == nil {
			for _, e := range entries {
				if e.IsDir() || strings.HasPrefix(e.Name(), tmpPrefix) {
					continue
				}
				if info, err := e.Info(); err == nil {
					n += info.Size()
				}
			}
		}
	}
	return n
}

// dirSize sums the sizes of every regular file under dir (0 when it does not exist).
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
	if os.IsNotExist(err) {
		return 0, nil
	}
	return n, err
}
