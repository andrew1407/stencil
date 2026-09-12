// Package clock is the server's single time seam. Everything that needs "now,
// in Unix milliseconds" reads it here, so a test freezes one clock instead of
// the four independent package-level vars this replaced (store, httpapi, auth,
// hub), which could disagree with each other.
package clock

import "time"

// NowMs reads the wall clock. Production never reassigns it; tests use Stub.
var NowMs = func() int64 { return time.Now().UnixMilli() }

// Stub points NowMs at fn and returns the func restoring the previous one.
func Stub(fn func() int64) func() {
	prev := NowMs
	NowMs = fn
	return func() { NowMs = prev }
}

// Fixed is a clock stopped at ms.
func Fixed(ms int64) func() int64 { return func() int64 { return ms } }
