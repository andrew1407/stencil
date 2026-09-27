// Cross-instance liveness: how often this instance publishes its live projects to Postgres, and how
// long another instance trusts that list, so no instance's sweep or delete takes a project in use.
package config

import (
	"fmt"
	"time"
)

// PresenceOptions sizes the presence heartbeat.
type PresenceOptions struct {
	TTL       time.Duration // PRESENCE_TTL_SECONDS: how long a published list stays trusted; 0 disables presence
	Heartbeat time.Duration // PRESENCE_HEARTBEAT_SECONDS: republish cadence, at most half the TTL
	Settle    time.Duration // PRESENCE_SETTLE_MS: the gap a burst of joins and leaves waits out before a beat
}

const (
	defaultPresenceTTL       = time.Minute
	defaultPresenceHeartbeat = 15 * time.Second // three missed beats before a list lapses
	defaultPresenceSettle    = 500 * time.Millisecond
)

func loadPresence(get getter, cfg *Config) error {
	p := &cfg.Presence
	var err error
	if p.TTL, err = duration(get, "PRESENCE_TTL_SECONDS", defaultPresenceTTL, time.Second, 0); err != nil {
		return err
	}
	if p.Heartbeat, err = duration(get, "PRESENCE_HEARTBEAT_SECONDS", defaultPresenceHeartbeat, time.Second, 1); err != nil {
		return err
	}
	if p.Settle, err = duration(get, "PRESENCE_SETTLE_MS", defaultPresenceSettle, time.Millisecond, 1); err != nil {
		return err
	}
	if p.TTL > 0 && 2*p.Heartbeat > p.TTL {
		return fmt.Errorf("config: PRESENCE_HEARTBEAT_SECONDS (%v) must be at most half PRESENCE_TTL_SECONDS (%v)",
			p.Heartbeat, p.TTL)
	}
	// No beat, the heartbeat's included, runs while a burst settles, so a longer settle could lapse the list.
	if p.TTL > 0 && p.Settle > p.Heartbeat {
		return fmt.Errorf("config: PRESENCE_SETTLE_MS (%v) must be at most PRESENCE_HEARTBEAT_SECONDS (%v)",
			p.Settle, p.Heartbeat)
	}
	return nil
}
