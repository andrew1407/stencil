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
	for _, k := range []string{"LISTEN_ADDR", "DATABASE_URL", "REDIS_URL", "FILESTORE_ROOT", "TLS_CERT", "TLS_KEY", "ADMIN_TOKEN", "TOKEN_TTL_HOURS", "MAX_BODY_BYTES"} {
		t.Setenv(k, "")
		os.Unsetenv(k)
	}
}
