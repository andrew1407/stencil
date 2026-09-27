package store

// DB-gated: session issuance and expiry.

import (
	"context"
	"errors"
	"testing"

	"stencil/server/internal/auth"
)

func TestSessionLifecycle(t *testing.T) {
	s := requireStore(t)
	ctx := context.Background()
	_, hash, _ := auth.GenerateToken()
	sess, err := s.CreateSession(ctx, hash, "cli", 1000, 9999)
	if err != nil {
		t.Fatal(err)
	}
	got, err := s.ResolveToken(ctx, hash)
	if err != nil {
		t.Fatalf("resolve: %v", err)
	}
	if got.ID != sess.ID || got.ExpiresAt != 9999 {
		t.Fatalf("resolved session mismatch: %+v", got)
	}
}

// The real Postgres → auth.Verify path (shared by the REST middleware and the WS hello handshake) rejects
// a session whose expiry has passed, even though ResolveToken returns the row unfiltered.
func TestExpiredSessionRejectedEndToEnd(t *testing.T) {
	s := requireStore(t)
	ctx := context.Background()
	token, hash, _ := auth.GenerateToken()
	// Session that expired at t=1000ms.
	if _, err := s.CreateSession(ctx, hash, "cli", 500, 1000); err != nil {
		t.Fatal(err)
	}
	// Verified "now" is well past expiry → rejected.
	if _, err := auth.Verify(ctx, s, token, 2000); !errors.Is(err, auth.ErrInvalidToken) {
		t.Fatalf("expired session should fail Verify, got %v", err)
	}
	// A live (future-expiry) session still verifies.
	token2, hash2, _ := auth.GenerateToken()
	if _, err := s.CreateSession(ctx, hash2, "cli", 500, 9_000_000_000_000); err != nil {
		t.Fatal(err)
	}
	if _, err := auth.Verify(ctx, s, token2, 2000); err != nil {
		t.Fatalf("live session should verify, got %v", err)
	}
}

// The sweep's session half: only rows past a non-zero expiry go, in bounded batches.
func TestDeleteExpiredSessions(t *testing.T) {
	s := requireStore(t)
	ctx := context.Background()
	for _, exp := range []int64{100, 200, 0, 9_000} {
		_, hash, _ := auth.GenerateToken()
		if _, err := s.CreateSession(ctx, hash, "t", 1, exp); err != nil {
			t.Fatal(err)
		}
	}
	n, err := s.DeleteExpiredSessions(ctx, 1_000, 1)
	if err != nil || n != 1 {
		t.Fatalf("first batch: %d %v, want 1", n, err)
	}
	if n, _ = s.DeleteExpiredSessions(ctx, 1_000, 10); n != 1 {
		t.Fatalf("second batch: %d, want the other expired row", n)
	}
	var left int
	if err := s.pool.QueryRow(ctx, `SELECT count(*) FROM sessions`).Scan(&left); err != nil || left != 2 {
		t.Fatalf("left %d (%v), want the never-expiring and the future session", left, err)
	}
}
