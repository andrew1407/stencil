package config

// The abuse guards and the auth posture: rate limits, CORS origins, and the
// admin token that is generated rather than left open.

import (
	"os"
	"testing"
)

func TestAdminTokenGeneratedWhenUnset(t *testing.T) {
	chdirTemp(t)
	clearEnv(t)
	cfg, err := Load()
	if err != nil {
		t.Fatal(err)
	}
	if cfg.AdminToken == "" || !cfg.AdminTokenGenerated {
		t.Fatalf("unset ADMIN_TOKEN should generate one: token=%q generated=%v",
			cfg.AdminToken, cfg.AdminTokenGenerated)
	}
	// Per-boot randomness: two Loads never mint the same token.
	cfg2, err := Load()
	if err != nil {
		t.Fatal(err)
	}
	if cfg2.AdminToken == cfg.AdminToken {
		t.Fatal("generated admin tokens should differ across boots")
	}
}

func TestExplicitAdminTokenKept(t *testing.T) {
	chdirTemp(t)
	clearEnv(t)
	t.Setenv("ADMIN_TOKEN", "x")
	cfg, err := Load()
	if err != nil {
		t.Fatal(err)
	}
	if cfg.AdminToken != "x" || cfg.AdminTokenGenerated {
		t.Fatalf("explicit ADMIN_TOKEN should be kept as-is: token=%q generated=%v",
			cfg.AdminToken, cfg.AdminTokenGenerated)
	}
}

func TestCORSOriginsConfig(t *testing.T) {
	chdirTemp(t)
	clearEnv(t)
	cfg, err := Load()
	if err != nil {
		t.Fatal(err)
	}
	if len(cfg.CORSOrigins) != 0 {
		t.Fatalf("default CORSOrigins should be empty (loopback only), got %v", cfg.CORSOrigins)
	}

	t.Setenv("CORS_ORIGINS", "a,b")
	if cfg, err = Load(); err != nil {
		t.Fatal(err)
	}
	if len(cfg.CORSOrigins) != 2 || cfg.CORSOrigins[0] != "a" || cfg.CORSOrigins[1] != "b" {
		t.Fatalf("CORS_ORIGINS=a,b should parse to [a b], got %v", cfg.CORSOrigins)
	}

	// "*" is honoured, but only when asked for explicitly.
	t.Setenv("CORS_ORIGINS", "*")
	if cfg, err = Load(); err != nil {
		t.Fatal(err)
	}
	if len(cfg.CORSOrigins) != 1 || cfg.CORSOrigins[0] != "*" {
		t.Fatalf("CORS_ORIGINS=* should parse to [*], got %v", cfg.CORSOrigins)
	}
}

func TestAbuseGuardDefaults(t *testing.T) {
	chdirTemp(t)
	clearEnv(t)
	for _, k := range []string{"AUTH_RATE_PER_MINUTE", "WRITE_RATE_PER_MINUTE", "STORAGE_QUOTA_BYTES"} {
		t.Setenv(k, "")
		os.Unsetenv(k)
	}
	cfg, err := Load()
	if err != nil {
		t.Fatal(err)
	}
	// Both rates are ON out of the box; the storage quota is opt-in.
	if cfg.AuthRatePerMin != defaultAuthRatePerMin || cfg.WriteRatePerMin != defaultWriteRatePerMin {
		t.Fatalf("rate guards should default on: auth=%d write=%d", cfg.AuthRatePerMin, cfg.WriteRatePerMin)
	}
	if cfg.StorageQuotaBytes != 0 {
		t.Fatalf("StorageQuotaBytes default should be 0 (unlimited), got %d", cfg.StorageQuotaBytes)
	}
}

func TestAbuseGuardOverridesAndValidation(t *testing.T) {
	chdirTemp(t)
	clearEnv(t)
	t.Setenv("AUTH_RATE_PER_MINUTE", "5")
	t.Setenv("WRITE_RATE_PER_MINUTE", "0") // explicit opt-out
	t.Setenv("STORAGE_QUOTA_BYTES", "1048576")
	cfg, err := Load()
	if err != nil {
		t.Fatal(err)
	}
	if cfg.AuthRatePerMin != 5 || cfg.WriteRatePerMin != 0 || cfg.StorageQuotaBytes != 1<<20 {
		t.Fatalf("overrides not applied: %d %d %d", cfg.AuthRatePerMin, cfg.WriteRatePerMin, cfg.StorageQuotaBytes)
	}
	for key, bad := range map[string]string{
		"AUTH_RATE_PER_MINUTE":  "-1",
		"WRITE_RATE_PER_MINUTE": "x",
		"STORAGE_QUOTA_BYTES":   "-1",
	} {
		t.Setenv("AUTH_RATE_PER_MINUTE", "5")
		t.Setenv("WRITE_RATE_PER_MINUTE", "0")
		t.Setenv("STORAGE_QUOTA_BYTES", "1048576")
		t.Setenv(key, bad)
		if _, err := Load(); err == nil {
			t.Fatalf("%s=%s should be rejected", key, bad)
		}
	}
}

// AUTH_OPEN defaults off, parses bool forms, and rejects garbage.
func TestAuthOpenConfig(t *testing.T) {
	chdirTemp(t)
	clearEnv(t)
	t.Setenv("AUTH_OPEN", "")
	os.Unsetenv("AUTH_OPEN")
	if cfg, err := Load(); err != nil || cfg.AuthOpen {
		t.Fatalf("default should be closed: %+v err %v", cfg.AuthOpen, err)
	}
	for _, v := range []string{"1", "true"} {
		t.Setenv("AUTH_OPEN", v)
		if cfg, err := Load(); err != nil || !cfg.AuthOpen {
			t.Fatalf("AUTH_OPEN=%s should open issuance: %v err %v", v, cfg.AuthOpen, err)
		}
	}
	t.Setenv("AUTH_OPEN", "0")
	if cfg, err := Load(); err != nil || cfg.AuthOpen {
		t.Fatalf("AUTH_OPEN=0 should stay closed: %v err %v", cfg.AuthOpen, err)
	}
	t.Setenv("AUTH_OPEN", "yes")
	if _, err := Load(); err == nil {
		t.Fatal("AUTH_OPEN=yes should be rejected")
	}
}
