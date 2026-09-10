package config

import (
	"os"
	"testing"
	"time"
)

// The Phase-1 hardening keys: the failed-hello rate, the per-operation store
// deadline, and the trusted-proxy list that decides whether X-Forwarded-For is
// believed at all.
func TestHardeningDefaults(t *testing.T) {
	chdirTemp(t)
	clearEnv(t)
	for _, k := range []string{"HELLO_RATE_PER_MINUTE", "OP_TIMEOUT_SECONDS", "TRUSTED_PROXY_CIDRS"} {
		t.Setenv(k, "")
		os.Unsetenv(k)
	}
	cfg, err := Load()
	if err != nil {
		t.Fatal(err)
	}
	if cfg.HelloRatePerMin != defaultHelloRatePerMin {
		t.Fatalf("HelloRatePerMin default: %d", cfg.HelloRatePerMin)
	}
	if cfg.OpTimeout != defaultOpTimeout {
		t.Fatalf("OpTimeout default: %v", cfg.OpTimeout)
	}
	// Empty by default: nobody is trusted, so a forwarded header changes nothing.
	if len(cfg.TrustedProxies) != 0 {
		t.Fatalf("TrustedProxies should default empty, got %v", cfg.TrustedProxies)
	}
}

func TestHardeningOverrides(t *testing.T) {
	chdirTemp(t)
	clearEnv(t)
	t.Setenv("HELLO_RATE_PER_MINUTE", "0") // explicit opt-out is allowed
	t.Setenv("OP_TIMEOUT_SECONDS", "3")
	t.Setenv("TRUSTED_PROXY_CIDRS", "10.0.0.0/8, 192.0.2.7 , ::1")
	cfg, err := Load()
	if err != nil {
		t.Fatal(err)
	}
	if cfg.HelloRatePerMin != 0 {
		t.Fatalf("0 should disable the hello limit, got %d", cfg.HelloRatePerMin)
	}
	if cfg.OpTimeout != 3*time.Second {
		t.Fatalf("OpTimeout: %v", cfg.OpTimeout)
	}
	want := []string{"10.0.0.0/8", "192.0.2.7/32", "::1/128"}
	if len(cfg.TrustedProxies) != len(want) {
		t.Fatalf("TrustedProxies: %v", cfg.TrustedProxies)
	}
	for i, w := range want {
		if got := cfg.TrustedProxies[i].String(); got != w {
			t.Errorf("TrustedProxies[%d] = %s, want %s", i, got, w)
		}
	}
}

// Bad input fails the boot rather than silently trusting nobody (or everybody).
func TestHardeningRejectsBadValues(t *testing.T) {
	for _, tc := range []struct{ key, val string }{
		{"TRUSTED_PROXY_CIDRS", "10.0.0.0/8, not-a-cidr"},
		{"TRUSTED_PROXY_CIDRS", "10.0.0.0/99"},
		{"HELLO_RATE_PER_MINUTE", "-1"},
		{"OP_TIMEOUT_SECONDS", "0"}, // no timeout at all is not a valid choice
		{"OP_TIMEOUT_SECONDS", "abc"},
	} {
		t.Run(tc.key+"="+tc.val, func(t *testing.T) {
			chdirTemp(t)
			clearEnv(t)
			t.Setenv(tc.key, tc.val)
			if _, err := Load(); err == nil {
				t.Fatalf("%s=%q should be rejected", tc.key, tc.val)
			}
		})
	}
}
