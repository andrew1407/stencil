// Parsing helpers for config.Load: the env-var readers, the .env scanner, and
// the per-boot admin token. Split out so config.go stays the list of tunables.
package config

import (
	"bufio"
	"crypto/rand"
	"encoding/base64"
	"fmt"
	"net/netip"
	"os"
	"strconv"
	"strings"
)

// getter reads one env var, falling back to a default.
type getter = func(key, def string) string

// positiveInt reads an integer env var through get, returning def when unset. min is the lowest accepted
// value: 0 where zero means "disabled", 1 otherwise.
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

// parseOrigins splits a comma-separated origin list, trimming blanks. Empty means "loopback origins
// only" (the fail-closed dev default); a list containing "*" means any origin and must be explicit.
func parseOrigins(raw string) []string {
	out := make([]string, 0)
	for _, part := range strings.Split(raw, ",") {
		if p := strings.TrimSpace(part); p != "" {
			out = append(out, p)
		}
	}
	return out
}

// generateAdminToken mints a random bootstrap admin token for this boot when ADMIN_TOKEN is unset, so
// issuance is never open by default. 256-bit, like session tokens (internal/auth).
func generateAdminToken() (string, error) {
	raw := make([]byte, 32)
	if _, err := rand.Read(raw); err != nil {
		return "", fmt.Errorf("config: admin token generation: %w", err)
	}
	return base64.RawURLEncoding.EncodeToString(raw), nil
}

// loadDotEnv parses a simple KEY=VALUE file; a missing file is not an error. Blank and '#' lines are
// ignored and surrounding quotes on the value are stripped.
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

// parseCIDRs reads TRUSTED_PROXY_CIDRS: comma-separated networks (a bare address is one host) whose
// X-Forwarded-For is believed. Empty = trust nobody, so limiters key on the peer.
func parseCIDRs(raw string) ([]netip.Prefix, error) {
	out := make([]netip.Prefix, 0)
	for _, part := range strings.Split(raw, ",") {
		p := strings.TrimSpace(part)
		if p == "" {
			continue
		}
		if prefix, err := netip.ParsePrefix(p); err == nil {
			out = append(out, prefix.Masked())
			continue
		}
		addr, err := netip.ParseAddr(p)
		if err != nil {
			return nil, fmt.Errorf("config: invalid TRUSTED_PROXY_CIDRS entry %q", p)
		}
		out = append(out, netip.PrefixFrom(addr.Unmap(), addr.Unmap().BitLen()))
	}
	return out, nil
}
