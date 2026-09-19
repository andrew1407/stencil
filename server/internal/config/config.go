// Package config loads server configuration from the environment, with an
// optional .env file (KEY=VALUE lines) layered underneath real env vars. No
// third-party config library: a small parser keeps this stdlib-only, mirroring
// mcp/src/config.rs.
package config

import (
	"fmt"
	"net/netip"
	"os"
	"strconv"
	"time"
)

// Config holds every tunable for the server.
type Config struct {
	ListenAddr          string        // host:port for the HTTP/WS listener
	TCPAddr             string        // host:port for the raw-TCP (NDJSON) edit listener
	DatabaseURL         string        // postgres connection string (pgx)
	RedisURL            string        // redis://... (empty disables the bus; in-memory fan-out only)
	Redis               RedisOptions  // go-redis client sizing for that URL (redis.go)
	FilestoreRoot       string        // root directory for the secured file store
	TokenTTL            time.Duration // lifetime of issued auth tokens
	ProjectTTL          time.Duration // default lifetime stamped on new projects; 0 = no expiry (off)
	SweepInterval       time.Duration // how often to sweep expired projects; 0 disables the sweep
	MaxBodyBytes        int64         // request body cap for REST writes
	TLSCert             string        // optional TLS cert path (enables HTTPS/WSS)
	TLSKey              string        // optional TLS key path
	AdminToken          string        // bootstrap token for issuing tokens; generated when unset
	AdminTokenGenerated bool          // true when AdminToken was generated this boot (print it once)
	AuthOpen            bool          // AUTH_OPEN: POST /auth/token needs no admin bearer (explicit opt-in)
	CORSOrigins         []string      // browser origins allowed to call the REST API; empty = loopback only, "*" = any
	AuthRatePerMin      int           // POST /auth/token attempts per minute, per client IP (0 = off)
	WriteRatePerMin     int           // per-session project creations + file uploads per minute (0 = off)
	HelloRatePerMin     int           // FAILED WS/TCP hello handshakes per minute, per client IP (0 = off)
	StorageQuotaBytes   int64         // aggregate filestore cap in bytes (0 = unlimited)
	OpTimeout           time.Duration // deadline around one REST store operation
	DBMaxConns          int           // DB_MAX_CONNS: pool ceiling (0 = pgx default)
	DBMinConns          int           // DB_MIN_CONNS: warm connections (0 = pgx default)
	DBStatementTimeout  time.Duration // DB_STATEMENT_TIMEOUT: per-statement cap (0 = none)
	// TrustedProxies are the peers whose X-Forwarded-For is believed when a
	// limiter keys on the client IP. Empty (default) ignores the header.
	TrustedProxies []netip.Prefix

	// LLM proxy (llm-contract.md §6); the key never leaves this process. The two key vars are deliberately NOT
	// merged: a key named for Anthropic must not reach another host through a provider switch (see main.go).
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
	// Both caps are ON by default — every accepted turn spends the operator's upstream; 0 opts out. 30/min is
	// far above human chat pace and still bounds a runaway client.
	defaultLLMRatePerMin  = 30
	defaultLLMMaxInFlight = 8
	// Abuse guards: 10 issuance attempts/min per IP is far above any legitimate
	// pace; 120 writes/min per session still allows a bulk sync. 0 = off.
	defaultAuthRatePerMin  = 10
	defaultWriteRatePerMin = 120
	// Only FAILED hellos spend this (a good token refunds), so it bounds a
	// brute-force loop while leaving room for a reconnect storm behind one NAT.
	defaultHelloRatePerMin = 30
	// One store call, not one request: long enough for a cold index scan, short
	// enough that a stuck query releases its pool connection.
	defaultOpTimeout = 10 * time.Second
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
	for _, load := range []func(getter, *Config) error{loadLLM, loadDB, loadRedis} {
		if err := load(get, &cfg); err != nil {
			return Config{}, err
		}
	}
	if cfg.AuthRatePerMin, err = positiveInt(get, "AUTH_RATE_PER_MINUTE", defaultAuthRatePerMin, 0); err != nil {
		return Config{}, err
	}
	if cfg.WriteRatePerMin, err = positiveInt(get, "WRITE_RATE_PER_MINUTE", defaultWriteRatePerMin, 0); err != nil {
		return Config{}, err
	}
	if cfg.HelloRatePerMin, err = positiveInt(get, "HELLO_RATE_PER_MINUTE", defaultHelloRatePerMin, 0); err != nil {
		return Config{}, err
	}
	opSeconds, err := positiveInt(get, "OP_TIMEOUT_SECONDS", int(defaultOpTimeout/time.Second), 1)
	if err != nil {
		return Config{}, err
	}
	cfg.OpTimeout = time.Duration(opSeconds) * time.Second
	if cfg.TrustedProxies, err = parseCIDRs(get("TRUSTED_PROXY_CIDRS", "")); err != nil {
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
	// Explicit opt-in to open token issuance (main.go warns loudly).
	if v := get("AUTH_OPEN", ""); v != "" {
		b, err := strconv.ParseBool(v)
		if err != nil {
			return Config{}, fmt.Errorf("config: invalid AUTH_OPEN %q", v)
		}
		cfg.AuthOpen = b
	}
	// Never run with silently open token issuance: an unset ADMIN_TOKEN gets a random
	// per-boot token instead (main.go prints it once so dev stays one-step).
	if cfg.AdminToken == "" {
		if cfg.AdminToken, err = generateAdminToken(); err != nil {
			return Config{}, err
		}
		cfg.AdminTokenGenerated = true
	}
	return cfg, nil
}
