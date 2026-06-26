// Package redisbus implements bus.Bus over Redis pub/sub, giving the hub
// cross-instance fan-out of edit and project events. go-redis is the one
// third-party dependency sanctioned for Redis access.
package redisbus

import (
	"context"

	"github.com/redis/go-redis/v9"

	"stencil/server/internal/bus"
)

// subBuffer mirrors the in-proc bus: a slow consumer drops rather than stalls.
const subBuffer = 64

// Bus is a Redis-backed implementation of bus.Bus.
type Bus struct {
	client *redis.Client
}

var _ bus.Bus = (*Bus)(nil)

// New parses redisURL (redis://[user:pass@]host:port/db), connects, and pings.
func New(ctx context.Context, redisURL string) (*Bus, error) {
	opt, err := redis.ParseURL(redisURL)
	if err != nil {
		return nil, err
	}
	client := redis.NewClient(opt)
	if err := client.Ping(ctx).Err(); err != nil {
		_ = client.Close()
		return nil, err
	}
	return &Bus{client: client}, nil
}

// Publish posts data to a Redis channel.
func (b *Bus) Publish(ctx context.Context, channel string, data []byte) error {
	return b.client.Publish(ctx, channel, data).Err()
}

// Subscribe opens a Redis subscription and pumps payloads onto a buffered Go
// channel. The unsubscribe func closes the subscription, which ends the pump
// goroutine and closes the returned channel.
func (b *Bus) Subscribe(ctx context.Context, channel string) (<-chan []byte, func()) {
	pubsub := b.client.Subscribe(ctx, channel)
	out := make(chan []byte, subBuffer)
	go func() {
		defer close(out)
		for msg := range pubsub.Channel() {
			select {
			case out <- []byte(msg.Payload):
			default: // consumer behind; drop (recoverable via version resync)
			}
		}
	}()
	return out, func() { _ = pubsub.Close() }
}

// Close disconnects the client.
func (b *Bus) Close() error { return b.client.Close() }
