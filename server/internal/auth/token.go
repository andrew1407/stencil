// Package auth issues and verifies bearer tokens. Tokens are opaque 256-bit
// random values; the server persists only their SHA-256 hash, so a database leak
// never exposes a usable credential. Verification is constant-time. All of this
// is standard-library crypto (crypto/rand, crypto/sha256, crypto/subtle).
package auth

import (
	"context"
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/base64"
	"errors"
)

// ErrInvalidToken is returned when a token is unknown, malformed, or expired.
var ErrInvalidToken = errors.New("auth: invalid token")

// Session is the authenticated principal resolved from a token.
type Session struct {
	ID        string
	Label     string
	CreatedAt int64 // epoch ms
	ExpiresAt int64 // epoch ms
}

// SessionResolver looks up a session by the SHA-256 hash of its token. Stores
// implement this. It must return ErrInvalidToken when no live session matches.
type SessionResolver interface {
	ResolveToken(ctx context.Context, tokenHash []byte) (Session, error)
}

// GenerateToken returns a new URL-safe base64 token carrying 256 bits of
// entropy, along with its hash for storage.
func GenerateToken() (token string, hash []byte, err error) {
	raw := make([]byte, 32)
	if _, err := rand.Read(raw); err != nil {
		return "", nil, err
	}
	token = base64.RawURLEncoding.EncodeToString(raw)
	return token, HashToken(token), nil
}

// HashToken returns the SHA-256 hash of a token string. This is what the store
// persists and compares against.
func HashToken(token string) []byte {
	sum := sha256.Sum256([]byte(token))
	return sum[:]
}

// ConstantTimeEqual compares two hashes without leaking timing information.
func ConstantTimeEqual(a, b []byte) bool {
	return subtle.ConstantTimeCompare(a, b) == 1
}

// Verify resolves a raw token to its session and checks expiry against nowMs.
// It is the single verification path used by both the REST middleware and the
// WebSocket hello handshake.
func Verify(ctx context.Context, resolver SessionResolver, token string, nowMs int64) (Session, error) {
	if token == "" {
		return Session{}, ErrInvalidToken
	}
	sess, err := resolver.ResolveToken(ctx, HashToken(token))
	if err != nil {
		return Session{}, ErrInvalidToken
	}
	if sess.ExpiresAt != 0 && sess.ExpiresAt <= nowMs {
		return Session{}, ErrInvalidToken
	}
	return sess, nil
}
