package config

import (
	"testing"
	"time"
)

// Unset, presence is on: a list trusted for a minute, republished four times inside it, a burst of
// joins and leaves settling for half a second.
func TestPresenceDefaults(t *testing.T) {
	clearTunables(t)
	cfg, err := Load()
	if err != nil {
		t.Fatal(err)
	}
	if want := (PresenceOptions{TTL: time.Minute, Heartbeat: 15 * time.Second, Settle: 500 * time.Millisecond}); cfg.Presence != want {
		t.Fatalf("presence defaults %+v, want %+v", cfg.Presence, want)
	}
}

// A zero TTL turns presence off whatever the heartbeat; a set pair is taken as given.
func TestPresenceOverrides(t *testing.T) {
	const settle = 500 * time.Millisecond
	for _, tc := range []struct {
		ttl, beat, settle string
		want              PresenceOptions
	}{
		{"0", "", "", PresenceOptions{TTL: 0, Heartbeat: 15 * time.Second, Settle: settle}},
		{"0", "300", "", PresenceOptions{TTL: 0, Heartbeat: 300 * time.Second, Settle: settle}},
		{"120", "60", "", PresenceOptions{TTL: 2 * time.Minute, Heartbeat: time.Minute, Settle: settle}},
		{"60", "15", "15000", PresenceOptions{TTL: time.Minute, Heartbeat: 15 * time.Second, Settle: 15 * time.Second}},
		{"0", "5", "9000", PresenceOptions{TTL: 0, Heartbeat: 5 * time.Second, Settle: 9 * time.Second}},
	} {
		clearTunables(t)
		t.Setenv("PRESENCE_TTL_SECONDS", tc.ttl)
		if tc.beat != "" {
			t.Setenv("PRESENCE_HEARTBEAT_SECONDS", tc.beat)
		}
		if tc.settle != "" {
			t.Setenv("PRESENCE_SETTLE_MS", tc.settle)
		}
		cfg, err := Load()
		if err != nil {
			t.Fatalf("ttl %s beat %s: %v", tc.ttl, tc.beat, err)
		}
		if cfg.Presence != tc.want {
			t.Fatalf("ttl %s beat %s: %+v, want %+v", tc.ttl, tc.beat, cfg.Presence, tc.want)
		}
	}
}

// A heartbeat that could let a list lapse between beats is refused, as are malformed values.
func TestPresenceRejectsBadValues(t *testing.T) {
	for _, tc := range [][2]string{{"30", "16"}, {"-1", "5"}, {"x", "5"}, {"60", "0"}} {
		clearTunables(t)
		t.Setenv("PRESENCE_TTL_SECONDS", tc[0])
		t.Setenv("PRESENCE_HEARTBEAT_SECONDS", tc[1])
		if _, err := Load(); err == nil {
			t.Errorf("PRESENCE_TTL_SECONDS=%s PRESENCE_HEARTBEAT_SECONDS=%s should be rejected", tc[0], tc[1])
		}
	}
}

// A settle longer than the heartbeat could let the list lapse mid-burst, so it is refused while presence
// is on, as are malformed values.
func TestPresenceSettleRejectsBadValues(t *testing.T) {
	for _, settle := range []string{"15001", "0", "-1", "x"} {
		clearTunables(t)
		t.Setenv("PRESENCE_SETTLE_MS", settle)
		if _, err := Load(); err == nil {
			t.Errorf("PRESENCE_SETTLE_MS=%s should be rejected", settle)
		}
	}
}
