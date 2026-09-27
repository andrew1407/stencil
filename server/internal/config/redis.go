// Redis client tunables (REDIS_*; every timeout in seconds).
package config

import (
	"time"

	"stencil/server/internal/redisbus"
)

// RedisOptions sizes the go-redis client, where a zero field keeps its own default, and the bus's
// wait for a new subscription.
type RedisOptions struct {
	PoolSize         int
	DialTimeout      time.Duration
	IOTimeout        time.Duration
	SubscribeTimeout time.Duration // REDIS_SUBSCRIBE_TIMEOUT_SECONDS: the wait for Redis to acknowledge a SUBSCRIBE
}

const defaultSubscribeTimeout = redisbus.DefaultSubscribeTimeout

func loadRedis(get getter, cfg *Config) error {
	var err error
	if cfg.Redis.PoolSize, err = positiveInt(get, "REDIS_POOL_SIZE", 0, 0); err != nil {
		return err
	}
	dial, err := positiveInt(get, "REDIS_DIAL_TIMEOUT", 0, 0)
	if err != nil {
		return err
	}
	io, err := positiveInt(get, "REDIS_IO_TIMEOUT", 0, 0)
	if err != nil {
		return err
	}
	cfg.Redis.DialTimeout = time.Duration(dial) * time.Second
	cfg.Redis.IOTimeout = time.Duration(io) * time.Second
	cfg.Redis.SubscribeTimeout, err = duration(get, "REDIS_SUBSCRIBE_TIMEOUT_SECONDS", defaultSubscribeTimeout, time.Second, 1)
	return err
}
