// Live-session tunables: the hub's queues, hello wait and goodbye write, the transports' keepalive and
// deadlines, and the bus subscriber buffer and drop warning. Every default is the value its package
// falls back to when handed none.
package config

import (
	"time"

	"stencil/server/internal/eventbus"
)

// LiveOptions sizes the live edit sessions and the sockets under them.
type LiveOptions struct {
	OutBuffer      int           // HUB_OUT_BUFFER: frames queued per member, and per session's store arm
	OutBudgetBytes int64         // HUB_OUT_BUDGET_BYTES: one member's queued backlog, in bytes
	HelloTimeout   time.Duration // HELLO_TIMEOUT_SECONDS: how long a fresh connection may take to say hello
	NoticeTimeout  time.Duration // HUB_NOTICE_TIMEOUT_SECONDS: one goodbye write (shutdown, token expiry)
	BusSubBuffer   int           // BUS_SUB_BUFFER: deliveries queued per bus subscriber before one drops
	BusDropWarn    time.Duration // BUS_DROP_WARN_INTERVAL_SECONDS: the shortest gap between two drop warnings
	WSPing         time.Duration // WS_PING_SECONDS: keepalive ping cadence
	WSPongTimeout  time.Duration // WS_PONG_TIMEOUT_SECONDS: an unanswered ping reaps the peer after this
	TCPIdle        time.Duration // TCP_IDLE_TIMEOUT_SECONDS: a TCP peer silent this long is reaped
	TCPWrite       time.Duration // TCP_WRITE_TIMEOUT_SECONDS: one frame's write deadline
}

const (
	defaultOutBuffer      = 256
	defaultOutBudgetBytes = 8 << 20 // a frame may be 16 MiB; one still fits on an empty queue
	defaultHelloTimeout   = 10 * time.Second
	defaultNoticeTimeout  = time.Second
	defaultBusSubBuffer   = eventbus.DefaultSubBuffer
	defaultBusDropWarn    = 30 * time.Second
	defaultWSPing         = 30 * time.Second
	defaultWSPongTimeout  = 10 * time.Second
	defaultTCPIdle        = 5 * time.Minute
	defaultTCPWrite       = 30 * time.Second // a peer that takes a whole frame no faster is wedged
)

func loadLive(get getter, cfg *Config) error {
	live := &cfg.Live
	var err error
	if live.OutBuffer, err = positiveInt(get, "HUB_OUT_BUFFER", defaultOutBuffer, 1); err != nil {
		return err
	}
	budget, err := positiveInt(get, "HUB_OUT_BUDGET_BYTES", defaultOutBudgetBytes, 1)
	if err != nil {
		return err
	}
	live.OutBudgetBytes = int64(budget)
	if live.BusSubBuffer, err = positiveInt(get, "BUS_SUB_BUFFER", defaultBusSubBuffer, 1); err != nil {
		return err
	}
	for _, d := range []struct {
		dst  *time.Duration
		key  string
		def  time.Duration
		unit time.Duration
	}{
		{&live.HelloTimeout, "HELLO_TIMEOUT_SECONDS", defaultHelloTimeout, time.Second},
		{&live.NoticeTimeout, "HUB_NOTICE_TIMEOUT_SECONDS", defaultNoticeTimeout, time.Second},
		{&live.BusDropWarn, "BUS_DROP_WARN_INTERVAL_SECONDS", defaultBusDropWarn, time.Second},
		{&live.WSPing, "WS_PING_SECONDS", defaultWSPing, time.Second},
		{&live.WSPongTimeout, "WS_PONG_TIMEOUT_SECONDS", defaultWSPongTimeout, time.Second},
		{&live.TCPIdle, "TCP_IDLE_TIMEOUT_SECONDS", defaultTCPIdle, time.Second},
		{&live.TCPWrite, "TCP_WRITE_TIMEOUT_SECONDS", defaultTCPWrite, time.Second},
	} {
		if *d.dst, err = duration(get, d.key, d.def, d.unit, 1); err != nil {
			return err
		}
	}
	return nil
}
