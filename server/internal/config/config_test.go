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
