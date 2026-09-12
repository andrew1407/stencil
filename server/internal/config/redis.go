// Redis client tunables (REDIS_*; both timeouts in seconds).
package config

import "time"

// RedisOptions sizes the go-redis client; a zero field keeps its own default.
type RedisOptions struct {
	PoolSize    int
	DialTimeout time.Duration
	IOTimeout   time.Duration
}

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
	return nil
}
