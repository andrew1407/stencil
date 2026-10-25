package config

import (
	"fmt"
	"net"
	"net/netip"
	"strings"
)

// broadestTrustedBits is the shortest TRUSTED_PROXY_CIDRS prefix taken without a warning: a wider one
// believes X-Forwarded-For from peers that are not a proxy, which pick their own limiter keys.
const broadestTrustedBits = 16

// Warnings are the boot lines for a posture that runs but invites abuse; nothing here refuses to start,
// so an existing deployment keeps working.
func (c Config) Warnings() []string {
	var out []string
	if c.StorageQuotaBytes == 0 && c.OwnerQuotaBytes == 0 && c.SessionQuotaBytes == 0 && !loopbackListen(c.ListenAddr) {
		out = append(out, fmt.Sprintf("WARNING: LISTEN_ADDR %q is reachable from other machines and every "+
			"STORAGE_QUOTA_* is 0 — one session may fill the disk at the write rate. Set "+
			"STORAGE_QUOTA_PER_SESSION_BYTES (or a total or per-owner quota).", c.ListenAddr))
	}
	for _, p := range c.TrustedProxies {
		bits := broadestTrustedBits
		if p.Addr().Is6() && !p.Addr().Is4In6() {
			bits = 48
		}
		if p.Bits() < bits || p.Addr().IsUnspecified() {
			out = append(out, fmt.Sprintf("WARNING: TRUSTED_PROXY_CIDRS names %s — every peer in it is believed "+
				"when it sets X-Forwarded-For, so it chooses its own rate-limit key. Name the proxies themselves.", p))
		}
	}
	return out
}

// loopbackListen reports a listen address only this machine can reach; ":8090" binds every interface.
func loopbackListen(addr string) bool {
	host, _, err := net.SplitHostPort(addr)
	if err != nil {
		return false
	}
	if strings.EqualFold(host, "localhost") {
		return true
	}
	ip, err := netip.ParseAddr(host)
	return err == nil && ip.IsLoopback()
}
