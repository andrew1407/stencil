package hub

// Throttle for the hello handshake. AcceptWS deliberately skips origin checks
// (the bearer token in the hello frame is the auth), so any web page can open a
// socket and try tokens at line rate — hello was the one credential check with
// no limiter, while AUTH_RATE_PER_MINUTE covered only POST /auth/token. A hello
// now spends a per-IP token before auth.Verify runs and refunds it when the
// token was good, so only FAILED handshakes consume the budget and an exhausted
// bucket is refused without touching the store.

import (
	"context"
	"errors"
	"net/http"
	"net/netip"

	"stencil/server/internal/auth"
	"stencil/server/internal/clock"
	"stencil/server/internal/protocol"
	"stencil/server/internal/ratelimit"
	"stencil/server/internal/transport"
)

// errHelloThrottled ends a connection refused by the hello limiter.
var errHelloThrottled = errors.New("hub: too many failed hello handshakes")

// helloGuard meters failed handshakes per client IP. The zero value is off.
type helloGuard struct {
	rate    *ratelimit.Limiter
	trusted []netip.Prefix
}

// Option configures a Hub at construction time (New).
type Option func(*Hub)

// WithHelloLimit meters failed hello verifications at perMin per client IP
// (0 = unlimited). trusted names the proxies whose X-Forwarded-For is believed
// for WebSocket handshakes; without it every browser behind one proxy would
// share a single bucket.
func WithHelloLimit(perMin int, trusted []netip.Prefix) Option {
	return func(h *Hub) {
		h.hello = helloGuard{rate: ratelimit.New(perMin), trusted: trusted}
	}
}

// clientIPKey carries the WebSocket handshake's resolved client IP into
// HandleConn: once upgraded, the conn knows only its peer, which behind a proxy
// is the proxy.
type clientIPKey struct{}

func withClientIP(ctx context.Context, ip string) context.Context {
	return context.WithValue(ctx, clientIPKey{}, ip)
}

// requestIP resolves the client IP of a WebSocket upgrade request.
func (g helloGuard) requestIP(req *http.Request) string {
	return ratelimit.ClientIP(req.RemoteAddr, req.Header.Get("X-Forwarded-For"), g.trusted)
}

// connIP is the limiter key for a connection: the IP resolved at the WS
// handshake when there is one, else the raw peer (TCP has no forwarded header).
func (g helloGuard) connIP(ctx context.Context, conn transport.Conn) string {
	if ip, ok := ctx.Value(clientIPKey{}).(string); ok {
		return ip
	}
	return ratelimit.ClientIP(conn.RemoteAddr(), "", nil)
}

// checkHello authenticates the hello frame under the per-IP failure limit. It
// writes the refusal frame and closes the connection itself; a nil return means
// the caller may join the session.
func (h *Hub) checkHello(ctx context.Context, conn transport.Conn, hello protocol.WSMessage) error {
	ip := h.hello.connIP(ctx, conn)
	if !h.hello.rate.Allow(ip) {
		refuseHello(ctx, conn, protocol.CodeRateLimited, "too many failed handshakes; retry later")
		return errHelloThrottled
	}
	if _, err := auth.Verify(ctx, h.resolver, hello.Token, clock.NowMs()); err != nil {
		refuseHello(ctx, conn, protocol.CodeUnauthorized, "invalid token")
		return err
	}
	h.hello.rate.Refund(ip) // a good token costs nothing
	return nil
}

// refuseHello tells the peer why and hangs up.
func refuseHello(ctx context.Context, conn transport.Conn, code, msg string) {
	writeMsg(ctx, conn, protocol.WSMessage{Type: protocol.WSError, Code: code, Message: msg})
	_ = conn.Close(transport.ClosePolicyViolation, msg)
}
