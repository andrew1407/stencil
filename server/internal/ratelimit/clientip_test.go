package ratelimit

import (
	"net/netip"
	"testing"
)

// ClientIP is what every per-IP bucket is keyed on, so a client must never be able to choose its own key:
// X-Forwarded-For counts only from a trusted peer, back to the rightmost hop it did not vouch for.
func TestClientIP(t *testing.T) {
	trusted := []netip.Prefix{
		netip.MustParsePrefix("127.0.0.0/8"),
		netip.MustParsePrefix("10.0.0.0/8"),
	}
	cases := []struct {
		name    string
		remote  string
		xff     string
		trusted []netip.Prefix
		want    string
	}{
		{"no trusted CIDRs ignores the header", "203.0.113.5:4444", "198.51.100.1", nil, "203.0.113.5"},
		{"spoofed header from an untrusted peer", "203.0.113.5:4444", "198.51.100.1", trusted, "203.0.113.5"},
		{"trusted peer takes the forwarded client", "127.0.0.1:5000", "198.51.100.1", trusted, "198.51.100.1"},
		{"client-prepended hops are ignored", "127.0.0.1:5000", "1.2.3.4, 5.6.7.8, 198.51.100.1", trusted, "198.51.100.1"},
		{"rightmost untrusted hop wins over trusted proxies", "127.0.0.1:5000", "198.51.100.1, 10.0.0.7", trusted, "198.51.100.1"},
		{"every hop trusted falls back to the peer", "127.0.0.1:5000", "10.0.0.7, 10.0.0.8", trusted, "127.0.0.1"},
		{"garbage left of the vouched hop is ignored", "127.0.0.1:5000", "not-an-ip, 198.51.100.1", trusted, "198.51.100.1"},
		{"a malformed rightmost hop falls back to the peer", "127.0.0.1:5000", "198.51.100.1, junk", trusted, "127.0.0.1"},
		{"empty header keeps the peer", "127.0.0.1:5000", "", trusted, "127.0.0.1"},
		{"a hop with a port still parses", "127.0.0.1:5000", "198.51.100.1:9", trusted, "198.51.100.1"},
		{"ipv6 peer without a port", "::1", "198.51.100.1", trusted, "::/64"},
		{"an ipv6 peer keys on its /64", "[2001:db8:1:2:aaaa::1]:443", "", nil, "2001:db8:1:2::/64"},
		{"a forwarded ipv6 client keys on its /64", "127.0.0.1:5000", "2001:db8:1:2:ffff::9", trusted, "2001:db8:1:2::/64"},
		{"an ipv4-mapped peer is ipv4", "[::ffff:203.0.113.5]:80", "", nil, "203.0.113.5"},
		{"ipv4-mapped hops normalise", "127.0.0.1:5000", "::ffff:198.51.100.1", trusted, "198.51.100.1"},
		{"bare host with no port", "203.0.113.5", "", nil, "203.0.113.5"},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			if got := ClientIP(tc.remote, tc.xff, tc.trusted); got != tc.want {
				t.Errorf("ClientIP(%q, %q) = %q, want %q", tc.remote, tc.xff, got, tc.want)
			}
		})
	}
}

// Two addresses in one /64 share a bucket; the neighbouring /64 does not.
func TestIPv6AddressesInOneSlash64ShareABucket(t *testing.T) {
	l := New(1)
	if !l.Allow(ClientIP("[2001:db8:0:1::a]:1", "", nil)) {
		t.Fatal("the first spend was refused")
	}
	if l.Allow(ClientIP("[2001:db8:0:1::b]:1", "", nil)) {
		t.Fatal("a fresh address in the same /64 got a fresh bucket")
	}
	if !l.Allow(ClientIP("[2001:db8:0:2::a]:1", "", nil)) {
		t.Fatal("the next /64 shares the bucket")
	}
}
