package auth

import (
	"bytes"
	"context"
	"net/http"
	"net/http/httptest"
	"testing"
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
	// The query-param fallback is reserved for WebSocket upgrade requests; on a
	// plain REST request a URL token is ignored (it would leak via logs).
	r := httptest.NewRequest(http.MethodGet, "/?token=fromquery", nil)
	if got := BearerToken(r); got != "" {
		t.Fatalf("query token on a plain request: got %q, want empty", got)
	}
	r.Header.Set("Upgrade", "websocket")
	r.Header.Set("Connection", "keep-alive, Upgrade")
	if got := BearerToken(r); got != "fromquery" {
		t.Fatalf("query fallback on upgrade: got %q", got)
	}
}

// A REST route must reject a query-only token; a WS upgrade may authenticate with one (browser WebSocket
// clients cannot set an Authorization header).
func TestMiddlewareQueryTokenOnlyOnWebSocketUpgrade(t *testing.T) {
	token, hash, _ := GenerateToken()
	res := stubResolver{hash: hash, sess: Session{ID: "s1"}} // ExpiresAt 0 = no expiry
	protected := Middleware(res)(http.HandlerFunc(func(rw http.ResponseWriter, _ *http.Request) {
		rw.WriteHeader(http.StatusOK)
	}))

	rest := httptest.NewRequest(http.MethodGet, "/projects?token="+token, nil)
	rec := httptest.NewRecorder()
	protected.ServeHTTP(rec, rest)
	if rec.Code != http.StatusUnauthorized {
		t.Fatalf("query-token REST request: code %d, want 401", rec.Code)
	}

	ws := httptest.NewRequest(http.MethodGet, "/ws?token="+token, nil)
	ws.Header.Set("Upgrade", "websocket")
	ws.Header.Set("Connection", "Upgrade")
	rec2 := httptest.NewRecorder()
	protected.ServeHTTP(rec2, ws)
	if rec2.Code != http.StatusOK {
		t.Fatalf("query-token WS upgrade: code %d, want 200", rec2.Code)
	}
}

func TestMiddlewareGate(t *testing.T) {
	token, hash, _ := GenerateToken()
	r := stubResolver{hash: hash, sess: Session{ID: "s1", ExpiresAt: 0}} // 0 = no expiry

	var sawSession string
	protected := Middleware(r)(http.HandlerFunc(func(rw http.ResponseWriter, req *http.Request) {
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
