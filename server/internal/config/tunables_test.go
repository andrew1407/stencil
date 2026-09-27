package config

import (
	"os"
	"testing"
	"time"
)

var tunableKeys = []string{"HUB_OUT_BUFFER", "HUB_OUT_BUDGET_BYTES", "HELLO_TIMEOUT_SECONDS", "BUS_SUB_BUFFER",
	"WS_PING_SECONDS", "WS_PONG_TIMEOUT_SECONDS", "TCP_IDLE_TIMEOUT_SECONDS", "TCP_WRITE_TIMEOUT_SECONDS",
	"HTTP_READ_HEADER_TIMEOUT_SECONDS", "HTTP_READ_TIMEOUT_SECONDS", "HTTP_WRITE_TIMEOUT_SECONDS",
	"HTTP_IDLE_TIMEOUT_SECONDS", "SHUTDOWN_TIMEOUT_SECONDS", "RETRY_AFTER_SECONDS", "LLM_BUSY_RETRY_AFTER_SECONDS",
	"PROJECTS_PAGE_SIZE", "SWEEP_BATCH", "SWEEP_WORKERS", "FILESTORE_RECONCILE_MINUTES", "FILESTORE_TMP_MAX_AGE_MINUTES",
	"PRESENCE_TTL_SECONDS", "PRESENCE_HEARTBEAT_SECONDS", "PRESENCE_SETTLE_MS", "HUB_NOTICE_TIMEOUT_SECONDS",
	"BUS_DROP_WARN_INTERVAL_SECONDS", "REDIS_SUBSCRIBE_TIMEOUT_SECONDS", "RATE_BUCKET_IDLE_MINUTES", "OP_TIMEOUT_SECONDS"}

func clearTunables(t *testing.T) {
	t.Helper()
	chdirTemp(t)
	clearEnv(t)
	for _, k := range tunableKeys {
		t.Setenv(k, "")
		os.Unsetenv(k)
	}
}

// Unset, every moved constant keeps the value its package hard-coded.
func TestTunableDefaultsMatchTheOldConstants(t *testing.T) {
	clearTunables(t)
	cfg, err := Load()
	if err != nil {
		t.Fatal(err)
	}
	wantLive := LiveOptions{OutBuffer: 256, OutBudgetBytes: 8 << 20, HelloTimeout: 10 * time.Second,
		NoticeTimeout: time.Second, BusSubBuffer: 64, BusDropWarn: 30 * time.Second, WSPing: 30 * time.Second,
		WSPongTimeout: 10 * time.Second, TCPIdle: 5 * time.Minute, TCPWrite: 30 * time.Second}
	if cfg.Live != wantLive {
		t.Fatalf("live defaults %+v, want %+v", cfg.Live, wantLive)
	}
	wantHTTP := HTTPOptions{ReadHeaderTimeout: 10 * time.Second, ReadTimeout: 5 * time.Minute,
		WriteTimeout: 5 * time.Minute, IdleTimeout: 2 * time.Minute, ShutdownTimeout: 10 * time.Second,
		RetryAfter: time.Minute, BusyRetryAfter: 5 * time.Second, ProjectsPageSize: 100}
	if cfg.HTTP != wantHTTP {
		t.Fatalf("http defaults %+v, want %+v", cfg.HTTP, wantHTTP)
	}
	wantSweep := SweepOptions{Batch: 500, Workers: 8, Reconcile: 6 * time.Hour, TmpMaxAge: time.Hour}
	if cfg.Sweep != wantSweep {
		t.Fatalf("sweep defaults %+v, want %+v", cfg.Sweep, wantSweep)
	}
	if cfg.Redis.SubscribeTimeout != 5*time.Second || cfg.RateBucketIdle != 10*time.Minute || cfg.OpTimeout != 10*time.Second {
		t.Fatalf("subscribe wait %v, bucket idle %v, op timeout %v; want 5s, 10m, 10s",
			cfg.Redis.SubscribeTimeout, cfg.RateBucketIdle, cfg.OpTimeout)
	}
}

func TestTunableOverrides(t *testing.T) {
	clearTunables(t)
	for k, v := range map[string]string{"HUB_OUT_BUFFER": "32", "HELLO_TIMEOUT_SECONDS": "3",
		"TCP_WRITE_TIMEOUT_SECONDS": "7", "RETRY_AFTER_SECONDS": "90", "PROJECTS_PAGE_SIZE": "50",
		"SWEEP_WORKERS": "2", "FILESTORE_RECONCILE_MINUTES": "0", "HUB_NOTICE_TIMEOUT_SECONDS": "2",
		"BUS_DROP_WARN_INTERVAL_SECONDS": "5", "REDIS_SUBSCRIBE_TIMEOUT_SECONDS": "9", "RATE_BUCKET_IDLE_MINUTES": "3"} {
		t.Setenv(k, v)
	}
	cfg, err := Load()
	if err != nil {
		t.Fatal(err)
	}
	if cfg.Live.OutBuffer != 32 || cfg.Live.HelloTimeout != 3*time.Second || cfg.Live.TCPWrite != 7*time.Second ||
		cfg.Live.NoticeTimeout != 2*time.Second || cfg.Live.BusDropWarn != 5*time.Second {
		t.Fatalf("live overrides: %+v", cfg.Live)
	}
	if cfg.Redis.SubscribeTimeout != 9*time.Second || cfg.RateBucketIdle != 3*time.Minute {
		t.Fatalf("subscribe wait %v, bucket idle %v; want 9s, 3m", cfg.Redis.SubscribeTimeout, cfg.RateBucketIdle)
	}
	if cfg.HTTP.RetryAfter != 90*time.Second || cfg.HTTP.ProjectsPageSize != 50 {
		t.Fatalf("http overrides: %+v", cfg.HTTP)
	}
	if cfg.Sweep.Workers != 2 || cfg.Sweep.Reconcile != 0 {
		t.Fatalf("sweep overrides: %+v (0 must disable the reconcile pass)", cfg.Sweep)
	}
}

func TestTunablesRejectBadValues(t *testing.T) {
	for _, tc := range [][2]string{{"HUB_OUT_BUFFER", "0"}, {"HELLO_TIMEOUT_SECONDS", "x"},
		{"WS_PING_SECONDS", "0"}, {"PROJECTS_PAGE_SIZE", "501"}, {"SWEEP_BATCH", "-1"},
		{"FILESTORE_TMP_MAX_AGE_MINUTES", "0"}, {"HUB_NOTICE_TIMEOUT_SECONDS", "0"},
		{"HUB_NOTICE_TIMEOUT_SECONDS", "11"}, {"BUS_DROP_WARN_INTERVAL_SECONDS", "x"},
		{"REDIS_SUBSCRIBE_TIMEOUT_SECONDS", "0"}, {"RATE_BUCKET_IDLE_MINUTES", "0"}} {
		clearTunables(t)
		t.Setenv(tc[0], tc[1])
		if _, err := Load(); err == nil {
			t.Errorf("%s=%q should be rejected", tc[0], tc[1])
		}
	}
}
