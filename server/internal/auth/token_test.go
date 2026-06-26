package auth

import (
	"context"
	"net/http"
	"net/http/httptest"
	"testing"
)

// fakeResolver maps a token hash to a session for tests.
type fakeResolver struct {
	hash []byte
	sess Session
}

func (f fakeResolver) ResolveToken(_ context.Context, hash []byte) (Session, error) {
	if f.hash != nil && ConstantTimeEqual(hash, f.hash) {
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
	if !ConstantTimeEqual(h1, HashToken(t1)) {
		t.Fatal("hash mismatch for generated token")
	}
	if ConstantTimeEqual(h1, HashToken(t2)) {
		t.Fatal("distinct tokens hashed equal")
	}
}

func TestVerifyExpiry(t *testing.T) {
	token, hash, _ := GenerateToken()
	r := fakeResolver{hash: hash, sess: Session{ID: "s1", ExpiresAt: 1000}}

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
	// Query-param fallback.
	r := httptest.NewRequest(http.MethodGet, "/?token=fromquery", nil)
	if got := BearerToken(r); got != "fromquery" {
		t.Fatalf("query fallback: got %q", got)
	}
}

func TestMiddlewareGate(t *testing.T) {
	token, hash, _ := GenerateToken()
	r := fakeResolver{hash: hash, sess: Session{ID: "s1", ExpiresAt: 0}} // 0 = no expiry

	var sawSession string
	protected := Middleware(r)(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		s, ok := SessionFromContext(req.Context())
		if !ok {
			t.Error("no session in context")
		}
		sawSession = s.ID
		w.WriteHeader(http.StatusOK)
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
