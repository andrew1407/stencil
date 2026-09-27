package service

// Cross-instance liveness: this instance publishes its live projects, and the sweep and the delete
// guard read every other instance's, so no instance deletes a project someone edits elsewhere.

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"time"
)

// Presence is one instance's view of the others: a random Instance id keys its rows, and another
// instance trusts a row for TTL after its last Beat.
type Presence struct {
	Store    PresenceStore
	Live     LiveCounts
	Instance string
	TTL      time.Duration
}

// NewPresence names this instance with a fresh random id.
func NewPresence(st PresenceStore, live LiveCounts, ttl time.Duration) (*Presence, error) {
	var b [12]byte
	if _, err := rand.Read(b[:]); err != nil {
		return nil, err
	}
	return &Presence{Store: st, Live: live, Instance: "i_" + hex.EncodeToString(b[:]), TTL: ttl}, nil
}

// Beat publishes this instance's live projects and drops the ones it has closed since the last beat.
func (p *Presence) Beat(ctx context.Context) error {
	return p.Store.BeatPresence(ctx, p.Instance, p.Live.LiveCounts(), p.TTL)
}

// LiveElsewhere lists the projects another instance holds a live session on.
func (p *Presence) LiveElsewhere(ctx context.Context) ([]string, error) {
	return p.Store.LiveElsewhere(ctx, p.Instance)
}

// RemoteMembers counts a project's live editors on every other instance.
func (p *Presence) RemoteMembers(ctx context.Context, projectID string) (int, error) {
	return p.Store.MembersElsewhere(ctx, p.Instance, projectID)
}
