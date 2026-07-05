// Package config loads server configuration from the environment, with an
// optional .env file (KEY=VALUE lines) layered underneath real env vars. No
// third-party config library: a small parser keeps this stdlib-only, mirroring
// mcp/src/config.rs.
package config

import (
	"bufio"
	"fmt"
	"os"
	"strconv"
	"strings"
	"time"
)

// Config holds every tunable for the server.
type Config struct {
	ListenAddr    string        // host:port for the HTTP/WS listener
	TCPAddr       string        // host:port for the raw-TCP (NDJSON) edit listener
	DatabaseURL   string        // postgres connection string (pgx)
	RedisURL      string        // redis://... (empty disables the bus; in-memory fan-out only)
	FilestoreRoot string        // root directory for the secured file store
	TokenTTL      time.Duration // lifetime of issued auth tokens
	ProjectTTL    time.Duration // default lifetime stamped on new projects; 0 = no expiry (off)
	SweepInterval time.Duration // how often to sweep expired projects; 0 disables the sweep
	MaxBodyBytes  int64         // request body cap for REST writes
	TLSCert       string        // optional TLS cert path (enables HTTPS/WSS)
	TLSKey        string        // optional TLS key path
	AdminToken    string        // optional bootstrap token for issuing tokens
	CORSOrigins   []string      // browser origins allowed to call the REST API ("*" = any)
}

// Defaults applied when the corresponding env var is unset.
const (
	defaultListenAddr   = ":8090"
	defaultTCPAddr      = ":8091"
	defaultFilestore    = "./data/filestore"
	defaultTokenTTL     = 7 * 24 * time.Hour
	defaultMaxBodyBytes = 32 << 20 // 32 MiB
	defaultSweep        = time.Hour // expired-project sweep cadence
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
		TokenTTL:      defaultTokenTTL,
		SweepInterval: defaultSweep,
		MaxBodyBytes:  defaultMaxBodyBytes,
		CORSOrigins:   parseOrigins(get("CORS_ORIGINS", "*")),
	}

	if v := get("TOKEN_TTL_HOURS", ""); v != "" {
		h, err := strconv.Atoi(v)
		if err != nil || h <= 0 {
			return Config{}, fmt.Errorf("config: invalid TOKEN_TTL_HOURS %q", v)
		}
		cfg.TokenTTL = time.Duration(h) * time.Hour
	}
	if v := get("MAX_BODY_BYTES", ""); v != "" {
		b, err := strconv.ParseInt(v, 10, 64)
		if err != nil || b <= 0 {
			return Config{}, fmt.Errorf("config: invalid MAX_BODY_BYTES %q", v)
		}
		cfg.MaxBodyBytes = b
	}
	// Default project lifetime, in hours. Unset/0 = off (server projects never
	// expire unless a client sets an explicit expiry).
	if v := get("PROJECT_TTL_HOURS", ""); v != "" {
		h, err := strconv.Atoi(v)
		if err != nil || h < 0 {
			return Config{}, fmt.Errorf("config: invalid PROJECT_TTL_HOURS %q", v)
		}
		cfg.ProjectTTL = time.Duration(h) * time.Hour
	}
	// Expired-project sweep cadence, in minutes. 0 disables the sweep entirely.
	if v := get("EXPIRY_SWEEP_MINUTES", ""); v != "" {
		m, err := strconv.Atoi(v)
		if err != nil || m < 0 {
			return Config{}, fmt.Errorf("config: invalid EXPIRY_SWEEP_MINUTES %q", v)
		}
		cfg.SweepInterval = time.Duration(m) * time.Minute
	}
	return cfg, nil
}

// parseOrigins splits a comma-separated origin list, trimming blanks. An empty
// value (or a list containing "*") means "any origin".
func parseOrigins(raw string) []string {
	out := []string{}
	for _, part := range strings.Split(raw, ",") {
		if p := strings.TrimSpace(part); p != "" {
			out = append(out, p)
		}
	}
	if len(out) == 0 {
		return []string{"*"}
	}
	return out
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
