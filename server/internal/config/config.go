// Package config loads server configuration from the environment, with an
// optional .env file (KEY=VALUE lines) layered underneath real env vars. No
// third-party config library: a small parser keeps this stdlib-only, mirroring
// mcp/src/config.rs.
package config

import (
	"bufio"
	"crypto/rand"
	"encoding/base64"
	"fmt"
	"os"
	"strconv"
	"strings"
	"time"
)

// Config holds every tunable for the server.
type Config struct {
	ListenAddr          string        // host:port for the HTTP/WS listener
	TCPAddr             string        // host:port for the raw-TCP (NDJSON) edit listener
	DatabaseURL         string        // postgres connection string (pgx)
	RedisURL            string        // redis://... (empty disables the bus; in-memory fan-out only)
	FilestoreRoot       string        // root directory for the secured file store
	TokenTTL            time.Duration // lifetime of issued auth tokens
	ProjectTTL          time.Duration // default lifetime stamped on new projects; 0 = no expiry (off)
	SweepInterval       time.Duration // how often to sweep expired projects; 0 disables the sweep
	MaxBodyBytes        int64         // request body cap for REST writes
	TLSCert             string        // optional TLS cert path (enables HTTPS/WSS)
	TLSKey              string        // optional TLS key path
	AdminToken          string        // bootstrap token for issuing tokens; generated when unset
	AdminTokenGenerated bool          // true when AdminToken was generated this boot (print it once)
	CORSOrigins         []string      // browser origins allowed to call the REST API; empty = loopback only, "*" = any
	AuthRatePerMin      int           // POST /auth/token attempts per minute, per client IP (0 = off)
	WriteRatePerMin     int           // per-session project creations + file uploads per minute (0 = off)
	StorageQuotaBytes   int64         // aggregate filestore cap in bytes (0 = unlimited)

	// LLM proxy (llm-contract.md §6). LLMProvider picks the upstream
	// mapping — anthropic (default) | ollama | openai-compat. The key never
	// leaves this process. Two key vars, deliberately NOT merged here: the
	// caller decides which applies, so a key named for Anthropic can't be
	// handed to some other host by a provider switch (see main.go).
	LLMProvider    string        // upstream mapping (§6.1/§6.2/§6.3)
	LLMAPIKey      string        // LLM_API_KEY — any provider
	AnthropicKey   string        // ANTHROPIC_API_KEY — legacy, Anthropic only
	LLMModel       string        // default model when a request names none
	LLMBaseURL     string        // upstream base URL (per-provider default when unset)
	LLMMaxTokens   int           // server-side cap; request maxTokens is clamped to it
	LLMTimeout     time.Duration // outbound request timeout for LLM calls
	LLMRatePerMin  int           // per-session /llm/chat turns per minute (0 = unlimited)
	LLMMaxInFlight int           // concurrent /llm/chat calls server-wide (0 = unlimited)
}

// Defaults applied when the corresponding env var is unset.
const (
	defaultListenAddr   = ":8090"
	defaultTCPAddr      = ":8091"
	defaultFilestore    = "./data/filestore"
	defaultTokenTTL     = 7 * 24 * time.Hour
	defaultMaxBodyBytes = 32 << 20  // 32 MiB
	defaultSweep        = time.Hour // expired-project sweep cadence
	defaultLLMModel     = "claude-opus-5"
	defaultLLMBaseURL   = "https://api.anthropic.com" // anthropic; see llm.DefaultBaseURL
	// Headroom for the biggest legitimate plan: a contract §2.1 multi-image turn
	// carrying a full set of traced outlines per image runs well past 8k.
	defaultLLMMaxTokens = 32768
	defaultLLMTimeout   = 120 * time.Second
	// Every accepted turn spends the operator's upstream, so both caps are ON by
	// default; 0 opts out explicitly. 30/min is far above human chat pace and
	// still bounds a runaway client.
	defaultLLMRatePerMin  = 30
	defaultLLMMaxInFlight = 8
	// Abuse guards: 10 issuance attempts/min per IP is far above any legitimate
	// pace; 120 writes/min per session still allows a bulk sync. 0 = off.
	defaultAuthRatePerMin  = 10
	defaultWriteRatePerMin = 120
)

// Load reads .env (if present in the working directory) then the process
// environment, the latter taking precedence, and returns the resolved Config.
func Load() (Config, error) {
	env, err := loadDotEnv(".env")
	if err != nil {
		return Config{}, err
	}
	get := func(key, def string) string {
		if v, ok := os.LookupEnv(key); ok {
			return v
		}
		if v, ok := env[key]; ok {
			return v
		}
		return def
	}

	cfg := Config{
		ListenAddr:    get("LISTEN_ADDR", defaultListenAddr),
		TCPAddr:       get("TCP_ADDR", defaultTCPAddr),
		DatabaseURL:   get("DATABASE_URL", ""),
		RedisURL:      get("REDIS_URL", ""),
		FilestoreRoot: get("FILESTORE_ROOT", defaultFilestore),
		TLSCert:       get("TLS_CERT", ""),
		TLSKey:        get("TLS_KEY", ""),
		AdminToken:    get("ADMIN_TOKEN", ""),
		MaxBodyBytes:  defaultMaxBodyBytes,
		CORSOrigins:   parseOrigins(get("CORS_ORIGINS", "")),
		LLMProvider:   get("LLM_PROVIDER", "anthropic"),
		LLMAPIKey:     get("LLM_API_KEY", ""),
		AnthropicKey:  get("ANTHROPIC_API_KEY", ""),
		LLMModel:      get("LLM_MODEL", defaultLLMModel),
		LLMBaseURL:    get("LLM_BASE_URL", ""), // "" = the provider's default
	}

	tokenTTLHours, err := positiveInt(get, "TOKEN_TTL_HOURS", int(defaultTokenTTL/time.Hour), 1)
	if err != nil {
		return Config{}, err
	}
	cfg.TokenTTL = time.Duration(tokenTTLHours) * time.Hour
	if v := get("MAX_BODY_BYTES", ""); v != "" {
		b, err := strconv.ParseInt(v, 10, 64)
		if err != nil || b <= 0 {
			return Config{}, fmt.Errorf("config: invalid MAX_BODY_BYTES %q", v)
		}
		cfg.MaxBodyBytes = b
	}
	// Default project lifetime, in hours. Unset/0 = off (server projects never
	// expire unless a client sets an explicit expiry).
	projectTTLHours, err := positiveInt(get, "PROJECT_TTL_HOURS", 0, 0)
	if err != nil {
		return Config{}, err
	}
	cfg.ProjectTTL = time.Duration(projectTTLHours) * time.Hour
	// Expired-project sweep cadence, in minutes. 0 disables the sweep entirely.
	sweepMinutes, err := positiveInt(get, "EXPIRY_SWEEP_MINUTES", int(defaultSweep/time.Minute), 0)
	if err != nil {
		return Config{}, err
	}
	cfg.SweepInterval = time.Duration(sweepMinutes) * time.Minute
	if cfg.LLMMaxTokens, err = positiveInt(get, "LLM_MAX_TOKENS", defaultLLMMaxTokens, 1); err != nil {
		return Config{}, err
	}
	llmTimeoutSeconds, err := positiveInt(get, "LLM_TIMEOUT_SECONDS", int(defaultLLMTimeout/time.Second), 1)
	if err != nil {
		return Config{}, err
	}
	cfg.LLMTimeout = time.Duration(llmTimeoutSeconds) * time.Second
	if cfg.LLMRatePerMin, err = positiveInt(get, "LLM_RATE_PER_MINUTE", defaultLLMRatePerMin, 0); err != nil {
		return Config{}, err
	}
	if cfg.LLMMaxInFlight, err = positiveInt(get, "LLM_MAX_IN_FLIGHT", defaultLLMMaxInFlight, 0); err != nil {
		return Config{}, err
	}
	if cfg.AuthRatePerMin, err = positiveInt(get, "AUTH_RATE_PER_MINUTE", defaultAuthRatePerMin, 0); err != nil {
		return Config{}, err
	}
	if cfg.WriteRatePerMin, err = positiveInt(get, "WRITE_RATE_PER_MINUTE", defaultWriteRatePerMin, 0); err != nil {
		return Config{}, err
	}
	// Aggregate filestore quota in bytes; unset/0 = unlimited.
	if v := get("STORAGE_QUOTA_BYTES", ""); v != "" {
		b, err := strconv.ParseInt(v, 10, 64)
		if err != nil || b < 0 {
			return Config{}, fmt.Errorf("config: invalid STORAGE_QUOTA_BYTES %q", v)
		}
		cfg.StorageQuotaBytes = b
	}
	// Never run with open token issuance: an unset ADMIN_TOKEN gets a random
	// per-boot token instead (main.go prints it once so dev stays one-step).
	if cfg.AdminToken == "" {
		if cfg.AdminToken, err = generateAdminToken(); err != nil {
			return Config{}, err
		}
		cfg.AdminTokenGenerated = true
	}
	return cfg, nil
}

// positiveInt reads an integer env var through get, returning def when unset.
// min is the lowest accepted value: 0 for settings where zero means "disabled",
// 1 otherwise. Non-numeric input or anything below min is rejected.
func positiveInt(get func(key, def string) string, key string, def, min int) (int, error) {
	v := get(key, "")
	if v == "" {
		return def, nil
	}
	n, err := strconv.Atoi(v)
	if err != nil || n < min {
		return 0, fmt.Errorf("config: invalid %s %q", key, v)
	}
	return n, nil
}

// parseOrigins splits a comma-separated origin list, trimming blanks. An empty
// result means "loopback origins only" (the fail-closed dev default); a list
// containing "*" means "any origin" and must be asked for explicitly.
func parseOrigins(raw string) []string {
	out := []string{}
	for _, part := range strings.Split(raw, ",") {
		if p := strings.TrimSpace(part); p != "" {
			out = append(out, p)
		}
	}
	return out
}

// generateAdminToken mints a random bootstrap admin token for this boot when
// ADMIN_TOKEN is unset, so issuance is never open by default. 256-bit, like
// session tokens (internal/auth).
func generateAdminToken() (string, error) {
	raw := make([]byte, 32)
	if _, err := rand.Read(raw); err != nil {
		return "", fmt.Errorf("config: admin token generation: %w", err)
	}
	return base64.RawURLEncoding.EncodeToString(raw), nil
}

// loadDotEnv parses a simple KEY=VALUE file. Missing file is not an error.
// Lines that are blank or start with '#' are ignored; surrounding quotes on the
// value are stripped.
func loadDotEnv(path string) (map[string]string, error) {
	f, err := os.Open(path)
	if err != nil {
		if os.IsNotExist(err) {
			return map[string]string{}, nil
		}
		return nil, err
	}
	defer f.Close()

	out := map[string]string{}
	sc := bufio.NewScanner(f)
	for sc.Scan() {
		line := strings.TrimSpace(sc.Text())
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		key, val, ok := strings.Cut(line, "=")
		if !ok {
			continue
		}
		key = strings.TrimSpace(key)
		val = strings.TrimSpace(val)
		val = strings.Trim(val, `"'`)
		if key != "" {
			out[key] = val
		}
	}
	return out, sc.Err()
}
