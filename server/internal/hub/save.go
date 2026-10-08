package hub

// A save's half of the session: one UpdateProject in flight per session, the newest waiting save behind
// it, and the committed outcome applied back on the run-loop.

import (
	"errors"
	"log"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/protocol"
	"stencil/server/internal/store"
)

// supersededNotice answers a save a later one replaced before it reached the store.
var supersededNotice = protocol.WSMessage{Type: protocol.WSError, Code: protocol.CodeConflict, Message: "superseded by a later save; reload"}

// handleSave dispatches the last-writer-wins UpdateProject to the worker, one at a time; a save arriving
// while one runs waits in the single pending slot, and a later one supersedes it there.
func (s *session) handleSave(m *member, msg protocol.WSMessage) {
	job := persistJob{kind: persistSave, member: m, layout: msg.Layout, version: msg.Version}
	if !s.saveInFlight {
		s.saveInFlight = true
		s.persist.dispatch(job)
		return
	}
	if old := s.pendingSave; old != nil && s.present(old.member) {
		s.sendMsg(old.member, supersededNotice)
	}
	s.pendingSave = &job
}

// nextSave hands the waiting save to the worker once the one in flight has answered.
func (s *session) nextSave() {
	s.saveInFlight = false
	if job := s.pendingSave; job != nil {
		s.pendingSave = nil
		s.saveInFlight = true
		s.persist.dispatch(*job)
	}
}

// applySaveResult applies a completed save on the run-loop: LWW/conflict/error handling, the version
// bump, the saver ack, the peer broadcast, and the global feed.
func (s *session) applySaveResult(res persistResult) {
	m := res.member
	switch {
	case errors.Is(res.err, store.ErrConflict):
		if s.present(m) {
			s.sendMsg(m, protocol.WSMessage{Type: protocol.WSError, Code: protocol.CodeConflict, Message: "stale version; reload"})
		}
		return
	case errors.Is(res.err, store.ErrNotFound):
		if s.present(m) {
			s.sendMsg(m, protocol.WSMessage{Type: protocol.WSError, Code: protocol.CodeNotFound, Message: "project gone"})
		}
		return
	case res.err != nil:
		log.Printf("hub: save project %s failed: %v", s.id, res.err)
		if s.present(m) {
			s.sendMsg(m, protocol.WSMessage{Type: protocol.WSError, Code: protocol.CodeInternal, Message: "save failed"})
		}
		return
	}
	rec := res.rec
	rec.Layout = s.loadedRec.Layout
	if len(res.layout) > 0 {
		rec.Layout = res.layout // an empty save leaves the stored layout, as the store's COALESCE does
	}
	s.version = rec.Version
	s.loadedRec = rec // keep the cached snapshot current with our own committed save
	// Ack the saver (if still connected) and broadcast the committed version to peers.
	if s.present(m) {
		s.sendMsg(m, protocol.WSMessage{Type: protocol.WSSynced, Version: rec.Version, ResultPath: rec.ResultPath})
	}
	synced := protocol.WSMessage{Type: protocol.WSSynced, Version: rec.Version, ResultPath: rec.ResultPath, FromClientID: m.clientID}
	s.publish(synced)
	// Notify the global feed so projects lists refresh live.
	eventbus.PublishProjectEvent(s.hub.ctx, s.hub.bus, protocol.EventUpdated, rec)
}

// present reports whether m is still the registered member for its client id
// (its out channel is open). Run-loop-only, so it is atomic vs. unregister.
func (s *session) present(m *member) bool {
	cur, ok := s.members[m.clientID]
	return ok && cur == m
}
