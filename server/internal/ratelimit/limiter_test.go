package ratelimit

import (
	"testing"
	"time"
)

// The bucket must allow a burst up to the per-minute rate, refuse the next
// spend, refill with elapsed time, cap that refill at one minute's worth, and
// keep keys independent — one noisy client can't starve another.
func TestLimiterBurstsRefillsAndKeepsKeysApart(t *testing.T) {
	now := time.Unix(1000, 0)
	l := New(3)
	l.now = func() time.Time { return now }

	steps := []struct {
		key     string
		advance time.Duration
		want    bool
	}{
		{"a", 0, true}, {"a", 0, true}, {"a", 0, true}, // burst up to capacity
		{"a", 0, false},                // bucket empty
		{"b", 0, true},                 // an independent key is unaffected
		{"a", 20 * time.Second, true},  // 20s at 3/min refills one token
		{"a", 0, false},                // and only one
		{"a", 10 * time.Minute, true},  // refill caps at one minute's worth
		{"a", 0, true}, {"a", 0, true}, // ...i.e. capacity 3
		{"a", 0, false}, // a long idle banks no more than the burst
	}
	for i, s := range steps {
		now = now.Add(s.advance)
		if got := l.Allow(s.key); got != s.want {
			t.Fatalf("step %d (%s): Allow=%v, want %v", i, s.key, got, s.want)
		}
	}
}

func TestZeroRateIsUnlimitedAndNilSafe(t *testing.T) {
	if l := New(0); l != nil {
		t.Fatal("0 = unlimited should allocate nothing")
	}
	var nilLimiter *Limiter
	for i := 0; i < 1000; i++ {
		if !nilLimiter.Allow("any") {
			t.Fatal("a nil limiter must never refuse")
		}
	}
}

func TestEvictsIdleBuckets(t *testing.T) {
	now := time.Unix(0, 0)
	l := New(5)
	l.now = func() time.Time { return now }
	l.Allow("old")
	now = now.Add(IdleTTL + time.Minute)
	l.Allow("new") // a new key triggers the sweep
	l.mu.Lock()
	_, stale := l.buckets["old"]
	l.mu.Unlock()
	if stale {
		t.Fatal("an idle bucket should not be retained for the process's lifetime")
	}
}

// Refund puts a spent token back, so a caller that meters only failures never
// drains a bucket with successful work. It can't push a bucket over capacity.
func TestRefundReturnsOneTokenAtMostToCapacity(t *testing.T) {
	l := New(2)
	l.Allow("a")
	l.Allow("a") // bucket empty
	if l.Allow("a") {
		t.Fatal("the bucket should be empty")
	}
	l.Refund("a")
	if !l.Allow("a") {
		t.Fatal("a refunded token should be spendable again")
	}
	for i := 0; i < 10; i++ {
		l.Refund("a")
	}
	if !l.Allow("a") || !l.Allow("a") {
		t.Fatal("capacity should be restored")
	}
	if l.Allow("a") {
		t.Fatal("refunds must not push a bucket past capacity")
	}
	var nilLimiter *Limiter
	nilLimiter.Refund("a") // nil-safe, like Allow
}
