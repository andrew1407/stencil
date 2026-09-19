package ratelimit

import (
	"net"
	"net/netip"
	"strings"
)

// ClientIP returns the address to key a limiter on. X-Forwarded-For is believed only when the peer itself
// is inside trusted, and then only back to the rightmost untrusted hop, so no client can pick its bucket.
func ClientIP(remoteAddr, xff string, trusted []netip.Prefix) string {
	peer := hostOf(remoteAddr)
	if len(trusted) == 0 || xff == "" || !isTrusted(peer, trusted) {
		return peer
	}
	hops := strings.Split(xff, ",")
	for i := len(hops) - 1; i >= 0; i-- {
		addr, err := netip.ParseAddr(hostOf(strings.TrimSpace(hops[i])))
		if err != nil {
			return peer // a malformed hop makes the whole chain untrustworthy
		}
		if hop := addr.Unmap().WithZone("").String(); !isTrusted(hop, trusted) {
			return hop
		}
	}
	return peer // every hop was a trusted proxy
}

// isTrusted reports whether host parses as an address inside one of the CIDRs.
func isTrusted(host string, trusted []netip.Prefix) bool {
	addr, err := netip.ParseAddr(host)
	if err != nil {
		return false
	}
	addr = addr.Unmap().WithZone("")
	for _, p := range trusted {
		if p.Contains(addr) {
			return true
		}
	}
	return false
}

// hostOf strips a port from "host:port"; a bare address is returned unchanged.
func hostOf(addr string) string {
	if host, _, err := net.SplitHostPort(addr); err == nil {
		return host
	}
	return addr
}
