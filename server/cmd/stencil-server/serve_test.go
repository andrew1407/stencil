package main

// newTLSConfig is the one boot step that turns on encryption for BOTH listeners, so
// what matters is that it is opt-in and that a misconfigured pair fails the boot
// rather than silently starting in plaintext.

import (
	"path/filepath"
	"testing"

	"stencil/server/internal/config"
)

// TLS is opt-in: only both halves together ask for it.
func TestBuildTLSIsOptIn(t *testing.T) {
	for _, tc := range []struct{ name, cert, key string }{
		{"neither", "", ""},
		{"cert only", "cert.pem", ""},
		{"key only", "", "tls.key"},
	} {
		conf, err := newTLSConfig(config.Config{TLSCert: tc.cert, TLSKey: tc.key})
		if err != nil {
			t.Fatalf("%s: %v", tc.name, err)
		}
		if conf != nil {
			t.Errorf("%s: half a pair must not enable TLS", tc.name)
		}
	}
}

// An unreadable pair fails the boot: starting in plaintext because a path was
// mistyped would silently un-encrypt both listeners.
func TestBuildTLSFailsOnAnUnreadablePair(t *testing.T) {
	dir := t.TempDir()
	cfg := config.Config{TLSCert: filepath.Join(dir, "nope.pem"), TLSKey: filepath.Join(dir, "nope.key")}
	if _, err := newTLSConfig(cfg); err == nil {
		t.Fatal("a missing certificate must fail the boot, not fall back to plaintext")
	}
}
