package service

import "sync"

// keyLock is a mutex per key, its entries dropped once nobody holds or waits on them. The zero value
// is ready to use.
type keyLock struct {
	mu   sync.Mutex
	held map[string]*keyEntry
}

type keyEntry struct {
	mu   sync.Mutex
	refs int
}

// lock blocks until key is free and answers the unlock.
func (k *keyLock) lock(key string) func() {
	k.mu.Lock()
	if k.held == nil {
		k.held = map[string]*keyEntry{}
	}
	e := k.held[key]
	if e == nil {
		e = &keyEntry{}
		k.held[key] = e
	}
	e.refs++
	k.mu.Unlock()
	e.mu.Lock()
	return func() {
		e.mu.Unlock()
		k.mu.Lock()
		if e.refs--; e.refs == 0 {
			delete(k.held, key)
		}
		k.mu.Unlock()
	}
}
