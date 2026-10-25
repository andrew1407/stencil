package hub

// The connection caps, per client IP and for the whole server: capacities, not rates, so they are
// counters beside the hello bucket. They are taken before the hello is read, which is what bounds the
// memory a flood of unauthenticated sockets holds; a refusal is counted, not logged one by one.

import (
	"errors"
	"log"
	"sync"
	"time"
)

var (
	// errTooManyConns ends a connection refused by the per-IP cap.
	errTooManyConns = errors.New("hub: too many connections from one address")
	// errServerFull ends a connection refused by the whole-server cap.
	errServerFull = errors.New("hub: the server is at its connection limit")
)

// ipCounter counts live connections per client IP and in all; a zero cap admits every connection.
type ipCounter struct {
	cap   int // per IP
	total int // the whole server
	mu    sync.Mutex
	live  map[string]int
	n     int
}

func newIPCounter(cap, total int) ipCounter {
	return ipCounter{cap: cap, total: total, live: map[string]int{}}
}

// take admits one more connection from ip, or names the cap that refused it.
func (c *ipCounter) take(ip string) error {
	if c.cap <= 0 && c.total <= 0 {
		return nil
	}
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.total > 0 && c.n >= c.total {
		return errServerFull
	}
	if c.cap > 0 && c.live[ip] >= c.cap {
		return errTooManyConns
	}
	c.live[ip]++
	c.n++
	return nil
}

// drop releases a connection take admitted.
func (c *ipCounter) drop(ip string) {
	if c.cap <= 0 && c.total <= 0 {
		return
	}
	c.mu.Lock()
	defer c.mu.Unlock()
	c.n--
	if c.live[ip] <= 1 {
		delete(c.live, ip)
		return
	}
	c.live[ip]--
}

// refusalLog sums the connections a cap or the hello throttle turned away and reports them at most
// once per `every`, so a flood writes one line a minute instead of one per socket.
type refusalLog struct {
	every time.Duration
	mu    sync.Mutex
	count int
	since time.Time
}

// refused reports whether err was a refusal (and counts it); anything else is the caller's to log.
func (r *refusalLog) refused(err error, now time.Time) bool {
	if !errors.Is(err, errTooManyConns) && !errors.Is(err, errServerFull) && !errors.Is(err, errHelloThrottled) {
		return false
	}
	r.mu.Lock()
	defer r.mu.Unlock()
	if r.since.IsZero() {
		r.since = now
	}
	r.count++
	if now.Sub(r.since) >= r.every {
		log.Printf("hub: %d connections refused (rate or connection caps) in the last %v", r.count, now.Sub(r.since).Round(time.Second))
		r.count, r.since = 0, now
	}
	return true
}
