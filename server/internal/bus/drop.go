package bus

// Both backends drop rather than stall when a subscriber is behind, and a silent
// drop reads like a lost edit. Report it — rate-limited, since the drop storm
// that matters would otherwise be the thing flooding the log.

import (
	"log"
	"sync"
	"time"
)

// dropWindow is the shortest gap between two warnings; a var so tests can widen
// or close it.
var dropWindow = 30 * time.Second

// DropLog counts dropped deliveries and warns at most once per dropWindow, with
// the number lost since the previous line. The zero value is ready to use.
type DropLog struct {
	mu     sync.Mutex
	n      int
	lastAt time.Time
}

// Drop records one delivery dropped by backend on channel.
func (d *DropLog) Drop(backend, channel string) {
	d.mu.Lock()
	d.n++
	n, now := d.n, time.Now()
	if !d.lastAt.IsZero() && now.Sub(d.lastAt) < dropWindow {
		d.mu.Unlock()
		return
	}
	d.n, d.lastAt = 0, now
	d.mu.Unlock()
	log.Printf("WARN %s: dropped %d message(s) to a slow subscriber on %q; "+
		"clients recover by version resync, but sustained drops mean a stuck peer", backend, n, channel)
}
