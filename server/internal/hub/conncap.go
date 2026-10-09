package hub

// The per-IP connection cap: a capacity, not a rate, so it is a counter beside the hello bucket. It is
// taken before the hello is read, which is what bounds the memory a flood of unauthenticated sockets holds.

import (
	"errors"
	"sync"
)

// errTooManyConns ends a connection refused by the per-IP cap.
var errTooManyConns = errors.New("hub: too many connections from one address")

// ipCounter counts live connections per client IP; a zero cap admits every connection.
type ipCounter struct {
	cap  int
	mu   sync.Mutex
	live map[string]int
}

func newIPCounter(cap int) ipCounter {
	return ipCounter{cap: cap, live: map[string]int{}}
}

// take admits one more connection from ip, or reports the cap reached.
func (c *ipCounter) take(ip string) bool {
	if c.cap <= 0 {
		return true
	}
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.live[ip] >= c.cap {
		return false
	}
	c.live[ip]++
	return true
}

// drop releases a connection take admitted.
func (c *ipCounter) drop(ip string) {
	if c.cap <= 0 {
		return
	}
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.live[ip] <= 1 {
		delete(c.live, ip)
		return
	}
	c.live[ip]--
}
