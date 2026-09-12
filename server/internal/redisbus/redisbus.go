// Package redisbus implements bus.Bus over Redis pub/sub, giving the hub
// cross-instance fan-out of edit and project events. go-redis is the one
// third-party dependency sanctioned for Redis access.
package redisbus

import (
	"context"
	"encoding/json"
	"time"

	"github.com/redis/go-redis/v9"

	"stencil/server/internal/bus"
)

// subBuffer mirrors the in-proc bus: a slow consumer drops rather than stalls.
const subBuffer = 64

// Options size the client; a zero field keeps go-redis's default.
type Options struct {
	PoolSize    int
	DialTimeout time.Duration
	IOTimeout   time.Duration
}

// redisBus is the Redis-backed implementation of bus.Bus.
type redisBus struct {
	client *redis.Client
	drops  bus.DropLog
}

var _ bus.Bus = (*redisBus)(nil)

// New parses redisURL (redis://[user:pass@]host:port/db), connects, and pings.
func New(ctx context.Context, redisURL string) (bus.Bus, error) {
	return NewWithOptions(ctx, redisURL, Options{})
}

// NewWithOptions is New with explicit sizing. The pool matters under fan-out:
// every session holds a subscription, and a publish needs a connection of its own.
func NewWithOptions(ctx context.Context, redisURL string, opts Options) (bus.Bus, error) {
	opt, err := redis.ParseURL(redisURL)
	if err != nil {
		return nil, err
	}
	if opts.PoolSize > 0 {
		opt.PoolSize = opts.PoolSize
	}
	if opts.DialTimeout > 0 {
		opt.DialTimeout = opts.DialTimeout
	}
	if opts.IOTimeout > 0 {
		opt.ReadTimeout, opt.WriteTimeout = opts.IOTimeout, opts.IOTimeout
	}
	client := redis.NewClient(opt)
	if err := client.Ping(ctx).Err(); err != nil {
		_ = client.Close()
		return nil, err
	}
	return &redisBus{client: client}, nil
}

// Publish posts one envelope to a Redis channel — the only place it is
// serialised. Data rides along as raw JSON, so the frame is not re-encoded.
func (b *redisBus) Publish(ctx context.Context, channel string, env bus.Envelope) error {
	payload, err := json.Marshal(env)
	if err != nil {
		return err
	}
	return b.client.Publish(ctx, channel, payload).Err()
}

// Subscribe opens a Redis subscription and pumps payloads onto a buffered Go
// channel. The unsubscribe func closes the subscription, which ends the pump
// goroutine and closes the returned channel.
func (b *redisBus) Subscribe(channel string) (<-chan bus.Envelope, func()) {
	// The subscription's lifetime is bounded by the returned unsubscribe func
	// (which closes the pubsub), not by a per-call context, so use a background
	// context for the initial SUBSCRIBE command.
	pubsub := b.client.Subscribe(context.Background(), channel)
	out := make(chan bus.Envelope, subBuffer)
	go func() {
		defer close(out)
		for msg := range pubsub.Channel() {
			var env bus.Envelope
			if json.Unmarshal([]byte(msg.Payload), &env) != nil {
				continue
			}
			select {
			case out <- env:
			default:
				b.drops.Drop("redisbus", channel)
			}
		}
	}()
	return out, func() { _ = pubsub.Close() }
}

// Close disconnects the client.
func (b *redisBus) Close() error { return b.client.Close() }
