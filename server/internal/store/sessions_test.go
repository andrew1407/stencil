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

// TestExpiredSessionRejectedEndToEnd proves the real Postgres → auth.Verify path (the
// single verification path shared by the REST middleware and the WS hello handshake)
// rejects a session whose expiry is in the past, even though ResolveToken itself
// returns the row unfiltered.
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
