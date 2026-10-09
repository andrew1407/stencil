package auth

import (
	"bytes"
	"context"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"
)

// stubResolver maps a token hash to a session for tests.
type stubResolver struct {
	hash []byte
	sess Session
}

func (f stubResolver) ResolveToken(_ context.Context, hash []byte) (Session, error) {
	if f.hash != nil && bytes.Equal(hash, f.hash) {
		return f.sess, nil
	}
	return Session{}, ErrInvalidToken
}

func TestGenerateTokenIsUniqueAndHashes(t *testing.T) {
	t1, h1, err := GenerateToken()
	if err != nil {
		t.Fatal(err)
	}
	t2, _, _ := GenerateToken()
	if t1 == t2 {
		t.Fatal("tokens not unique")
	}
	if !bytes.Equal(h1, HashToken(t1)) {
		t.Fatal("hash mismatch for generated token")
	}
	if bytes.Equal(h1, HashToken(t2)) {
		t.Fatal("distinct tokens hashed equal")
	}
}

func TestVerifyExpiry(t *testing.T) {
	token, hash, _ := GenerateToken()
	r := stubResolver{hash: hash, sess: Session{ID: "s1", ExpiresAt: 1000}}

	if _, err := Verify(context.Background(), r, token, 500); err != nil {
		t.Fatalf("live token rejected: %v", err)
	}
	if _, err := Verify(context.Background(), r, token, 1000); err == nil {
		t.Fatal("expired token accepted (boundary)")
	}
	if _, err := Verify(context.Background(), r, token, 2000); err == nil {
		t.Fatal("expired token accepted")
	}
	if _, err := Verify(context.Background(), r, "wrong", 500); err == nil {
		t.Fatal("unknown token accepted")
	}
	if _, err := Verify(context.Background(), r, "", 500); err == nil {
		t.Fatal("empty token accepted")
	}
}

func TestBearerTokenExtraction(t *testing.T) {
	cases := map[string]string{
		"Bearer abc": "abc",
		"bearer xyz": "xyz",
		"BEARER  q ": "q",
		"raw-token":  "raw-token",
	}
	for header, want := range cases {
		r := httptest.NewRequest(http.MethodGet, "/", nil)
		r.Header.Set("Authorization", header)
		if got := BearerToken(r); got != want {
			t.Fatalf("header %q: got %q want %q", header, got, want)
		}
	}
}

func TestMiddlewareGate(t *testing.T) {
	token, hash, _ := GenerateToken()
	r := stubResolver{hash: hash, sess: Session{ID: "s1", ExpiresAt: 0}} // 0 = no expiry

	var sawSession string
	protected := Middleware(r, 0)(http.HandlerFunc(func(rw http.ResponseWriter, req *http.Request) {
		s, ok := SessionFromContext(req.Context())
		if !ok {
			t.Error("no session in context")
		}
		sawSession = s.ID
		rw.WriteHeader(http.StatusOK)
	}))

	// Authorized.
	req := httptest.NewRequest(http.MethodGet, "/projects", nil)
	req.Header.Set("Authorization", "Bearer "+token)
	rec := httptest.NewRecorder()
	protected.ServeHTTP(rec, req)
	if rec.Code != http.StatusOK || sawSession != "s1" {
		t.Fatalf("authorized request failed: code=%d sess=%q", rec.Code, sawSession)
	}

	// Unauthorized.
	req2 := httptest.NewRequest(http.MethodGet, "/projects", nil)
	req2.Header.Set("Authorization", "Bearer nope")
	rec2 := httptest.NewRecorder()
	protected.ServeHTTP(rec2, req2)
	if rec2.Code != http.StatusUnauthorized {
		t.Fatalf("unauthorized request not blocked: code=%d", rec2.Code)
	}
}

// A URL token is never read, on any route and whatever upgrade headers ride along: only the Authorization
// header authenticates a REST request, and the WebSocket hello carries its own token.
func TestQueryTokenIsNeverRead(t *testing.T) {
	token, hash, _ := GenerateToken()
	res := stubResolver{hash: hash, sess: Session{ID: "s1"}}
	protected := Middleware(res, time.Second)(http.HandlerFunc(func(rw http.ResponseWriter, _ *http.Request) {
		rw.WriteHeader(http.StatusOK)
	}))
	for _, path := range []string{"/projects", "/auth/token", WSRoute, "/llm/chat"} {
		req := httptest.NewRequest(http.MethodGet, path+"?token="+token, nil)
		req.Header.Set("Upgrade", "websocket")
		req.Header.Set("Connection", "Upgrade")
		rec := httptest.NewRecorder()
		protected.ServeHTTP(rec, req)
		if rec.Code != http.StatusUnauthorized {
			t.Fatalf("%s with a query token: code %d, want 401", path, rec.Code)
		}
	}
}
