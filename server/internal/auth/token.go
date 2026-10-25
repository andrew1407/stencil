// Package auth issues and verifies bearer tokens. Tokens are opaque 256-bit
// random values; the server persists only their SHA-256 hash, so a database leak
// never exposes a usable credential. A bearer token is resolved by hash lookup,
// never by comparing secrets here. All of this is standard-library crypto
// (crypto/rand, crypto/sha256).
package auth

import (
	"context"
	"crypto/rand"
	"crypto/sha256"
	"encoding/base64"
	"errors"
	"fmt"
)

// ErrInvalidToken is returned when a token is unknown, malformed, or expired.
var ErrInvalidToken = errors.New("auth: invalid token")

// ErrLookupFailed wraps a store failure during the lookup: the token's validity is unknown, so a
// caller answers "retry", never "unauthorized" — a database blip must not log every client out.
var ErrLookupFailed = errors.New("auth: token lookup failed")

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

// Verify resolves a raw token to its session and checks expiry against nowMs — the single verification
// path for both the REST middleware and the WebSocket hello handshake.
func Verify(ctx context.Context, resolver SessionResolver, token string, nowMs int64) (Session, error) {
	if token == "" {
		return Session{}, ErrInvalidToken
	}
	sess, err := resolver.ResolveToken(ctx, HashToken(token))
	if errors.Is(err, ErrInvalidToken) {
		return Session{}, ErrInvalidToken
	}
	if err != nil {
		return Session{}, fmt.Errorf("%w: %w", ErrLookupFailed, err)
	}
	if sess.ExpiresAt != 0 && sess.ExpiresAt <= nowMs {
		return Session{}, ErrInvalidToken
	}
	return sess, nil
}
