package config

import (
	"os"
	"path/filepath"
	"testing"
	"time"
)

func TestLoadDefaults(t *testing.T) {
	chdirTemp(t)
	clearEnv(t)
	cfg, err := Load()
	if err != nil {
		t.Fatal(err)
	}
	if cfg.ListenAddr != defaultListenAddr {
		t.Fatalf("ListenAddr default: %q", cfg.ListenAddr)
	}
	if cfg.TokenTTL != defaultTokenTTL {
		t.Fatalf("TokenTTL default: %v", cfg.TokenTTL)
	}
	if cfg.MaxBodyBytes != defaultMaxBodyBytes {
		t.Fatalf("MaxBodyBytes default: %d", cfg.MaxBodyBytes)
	}
	if cfg.ProjectTTL != 0 {
		t.Fatalf("ProjectTTL default should be 0 (off), got %v", cfg.ProjectTTL)
	}
	if cfg.SweepInterval != defaultSweep {
		t.Fatalf("SweepInterval default: %v", cfg.SweepInterval)
	}
}

func TestLLMSpendControlDefaults(t *testing.T) {
	chdirTemp(t)
	clearEnv(t)
	cfg, err := Load()
	if err != nil {
		t.Fatal(err)
	}
	// Both caps are ON out of the box — an operator shouldn't have to discover
	// that an authenticated token can spend their upstream without limit.
	if cfg.LLMRatePerMin != defaultLLMRatePerMin || cfg.LLMMaxInFlight != defaultLLMMaxInFlight {
		t.Fatalf("spend controls should default on: %d/min, %d in flight",
			cfg.LLMRatePerMin, cfg.LLMMaxInFlight)
	}
	// The two key vars stay separate; main.go decides which applies.
	t.Setenv("ANTHROPIC_API_KEY", "sk-ant")
	t.Setenv("LLM_API_KEY", "sk-any")
	t.Setenv("LLM_RATE_PER_MINUTE", "0") // explicit opt-out is allowed
	if cfg, err = Load(); err != nil {
		t.Fatal(err)
	}
	if cfg.AnthropicKey != "sk-ant" || cfg.LLMAPIKey != "sk-any" {
		t.Fatalf("key vars should not be merged: %+v", cfg)
	}
	if cfg.LLMRatePerMin != 0 {
		t.Fatalf("0 should disable the rate limit, got %d", cfg.LLMRatePerMin)
	}
	t.Setenv("LLM_MAX_IN_FLIGHT", "-1")
	if _, err := Load(); err == nil {
		t.Fatal("a negative cap should be rejected, not silently treated as off")
	}
}

func TestExpiryConfig(t *testing.T) {
	chdirTemp(t)
	clearEnv(t)
	t.Setenv("PROJECT_TTL_HOURS", "48")
	t.Setenv("EXPIRY_SWEEP_MINUTES", "15")
	cfg, err := Load()
	if err != nil {
		t.Fatal(err)
	}
	if cfg.ProjectTTL != 48*time.Hour {
		t.Fatalf("PROJECT_TTL_HOURS not applied: %v", cfg.ProjectTTL)
	}
	if cfg.SweepInterval != 15*time.Minute {
		t.Fatalf("EXPIRY_SWEEP_MINUTES not applied: %v", cfg.SweepInterval)
	}

	// 0 sweep minutes disables the sweep.
	t.Setenv("EXPIRY_SWEEP_MINUTES", "0")
	cfg, err = Load()
	if err != nil || cfg.SweepInterval != 0 {
		t.Fatalf("EXPIRY_SWEEP_MINUTES=0 should disable sweep: %v %v", err, cfg.SweepInterval)
	}

	// Invalid values are rejected.
	t.Setenv("PROJECT_TTL_HOURS", "-3")
	if _, err := Load(); err == nil {
		t.Fatal("expected error for negative PROJECT_TTL_HOURS")
	}
}

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

func TestEnvOverridesDotEnv(t *testing.T) {
	dir := chdirTemp(t)
	clearEnv(t)
	dotenv := "LISTEN_ADDR=:1111\nDATABASE_URL=postgres://fromfile\nTOKEN_TTL_HOURS=24\n# comment\nREDIS_URL=\"redis://quoted\"\n"
	if err := os.WriteFile(filepath.Join(dir, ".env"), []byte(dotenv), 0o644); err != nil {
		t.Fatal(err)
	}
	t.Setenv("LISTEN_ADDR", ":2222") // real env wins over .env

	cfg, err := Load()
	if err != nil {
		t.Fatal(err)
	}
	if cfg.ListenAddr != ":2222" {
		t.Fatalf("env should override .env: %q", cfg.ListenAddr)
	}
	if cfg.DatabaseURL != "postgres://fromfile" {
		t.Fatalf("dotenv value lost: %q", cfg.DatabaseURL)
	}
	if cfg.RedisURL != "redis://quoted" {
		t.Fatalf("quote stripping failed: %q", cfg.RedisURL)
	}
	if cfg.TokenTTL != 24*time.Hour {
		t.Fatalf("TOKEN_TTL_HOURS not applied: %v", cfg.TokenTTL)
	}
}

func TestInvalidTTLRejected(t *testing.T) {
	chdirTemp(t)
	clearEnv(t)
	t.Setenv("TOKEN_TTL_HOURS", "notanumber")
	if _, err := Load(); err == nil {
		t.Fatal("expected error for invalid TOKEN_TTL_HOURS")
	}
}

// chdirTemp switches the working directory to a fresh temp dir so .env probing
// is isolated, restoring the original on cleanup.
func chdirTemp(t *testing.T) string {
	t.Helper()
	orig, _ := os.Getwd()
	dir := t.TempDir()
	if err := os.Chdir(dir); err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { os.Chdir(orig) })
	return dir
}

func clearEnv(t *testing.T) {
	t.Helper()
	for _, k := range []string{"LISTEN_ADDR", "DATABASE_URL", "REDIS_URL", "FILESTORE_ROOT", "TLS_CERT", "TLS_KEY", "ADMIN_TOKEN", "CORS_ORIGINS", "TOKEN_TTL_HOURS", "MAX_BODY_BYTES", "PROJECT_TTL_HOURS", "EXPIRY_SWEEP_MINUTES",
		"LLM_PROVIDER", "LLM_API_KEY", "ANTHROPIC_API_KEY", "LLM_BASE_URL", "LLM_RATE_PER_MINUTE", "LLM_MAX_IN_FLIGHT"} {
		t.Setenv(k, "")
		os.Unsetenv(k)
	}
}

// ----- abuse-guard config (AUTH_RATE_PER_MINUTE / WRITE_RATE_PER_MINUTE /
// STORAGE_QUOTA_BYTES) -----

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
