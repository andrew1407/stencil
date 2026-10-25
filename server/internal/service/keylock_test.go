package service

import (
	"sync"
	"sync/atomic"
	"testing"
	"time"
)

// One key is held by one caller at a time, other keys never wait on it, and the table empties again.
func TestKeyLockSerialisesOneKeyOnly(t *testing.T) {
	var k keyLock
	var inside, peak atomic.Int32
	var wg sync.WaitGroup
	for i := 0; i < 8; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			unlock := k.lock("p_1")
			if n := inside.Add(1); n > peak.Load() {
				peak.Store(n)
			}
			time.Sleep(2 * time.Millisecond)
			inside.Add(-1)
			unlock()
		}()
	}
	held := k.lock("p_1")
	other := make(chan struct{})
	go func() { k.lock("p_2")(); close(other) }()
	select {
	case <-other:
	case <-time.After(time.Second):
		t.Fatal("another key waited on p_1")
	}
	held()
	wg.Wait()
	if peak.Load() != 1 {
		t.Fatalf("%d callers held p_1 at once", peak.Load())
	}
	if len(k.held) != 0 {
		t.Fatalf("the table kept %d entries after every unlock", len(k.held))
	}
}
