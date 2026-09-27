package store

// DB-gated: the per-session ledger behind STORAGE_QUOTA_PER_SESSION_BYTES — the cap, a replacement moving
// the charge, a credit, the project and session cascades, the boot clear, and one session's uploads
// checked one at a time.

import (
	"context"
	"errors"
	"strconv"
	"sync"
	"testing"

	"stencil/server/internal/auth"
	"stencil/server/internal/protocol"
)

var errOver = errors.New("over the cap")

// capOf is fits for a cap, recording the n it was asked about.
func capOf(limit, n int64) func(int64) error {
	return func(held int64) error {
		if held+n > limit {
			return errOver
		}
		return nil
	}
}

func chargeRig(t *testing.T) (*Store, func() string, func(owner string) string) {
	t.Helper()
	s := requireStore(t)
	ctx := context.Background()
	session := func() string {
		_, hash, _ := auth.GenerateToken()
		sess, err := s.CreateSession(ctx, hash, "t", 1000, 0)
		if err != nil {
			t.Fatal(err)
		}
		return sess.ID
	}
	project := func(owner string) string {
		p, err := s.CreateProject(ctx, owner, protocol.CreateProjectRequest{Name: "C", HasImage: true})
		if err != nil {
			t.Fatal(err)
		}
		return p.ID
	}
	return s, session, project
}

func held(t *testing.T, s *Store, session string) int64 {
	t.Helper()
	n, err := s.SessionCharged(context.Background(), session)
	if err != nil {
		t.Fatal(err)
	}
	return n
}

func TestChargesFollowTheWriter(t *testing.T) {
	s, session, project := chargeRig(t)
	ctx := context.Background()
	a, b := session(), session()
	p, q := project(a), project(b)
	admit := func(sess, id, kind string, n int64) error {
		return s.Admit(ctx, Charge{ProjectID: id, Kind: kind, SessionID: sess, Bytes: n}, capOf(100, n), func() error { return nil })
	}
	if err := admit(a, p, protocol.KindOriginal, 60); err != nil {
		t.Fatal(err)
	}
	// b's project, a's bytes: the writer is charged, never the owner.
	if err := admit(a, q, protocol.KindOriginal, 60); !errors.Is(err, errOver) {
		t.Fatalf("past a's cap in another's project: %v", err)
	}
	if err := admit(a, p, protocol.KindOriginal, 95); err != nil || held(t, s, a) != 95 {
		t.Fatalf("replacing one's own file counts it once: %v, held %d", err, held(t, s, a))
	}
	if err := admit(b, p, protocol.KindOriginal, 30); err != nil || held(t, s, a) != 0 || held(t, s, b) != 30 {
		t.Fatalf("a replacement moves the charge: %v, a %d, b %d", err, held(t, s, a), held(t, s, b))
	}
	if err := s.Credit(ctx, p, protocol.KindOriginal); err != nil || held(t, s, b) != 0 {
		t.Fatalf("a credit drops the charge: %v, b %d", err, held(t, s, b))
	}
	if err := admit(a, q, protocol.KindResult, 40); err != nil {
		t.Fatal(err)
	}
	if err := s.DeleteProject(ctx, q); err != nil || held(t, s, a) != 0 {
		t.Fatalf("a deleted project takes its charges: %v, a %d", err, held(t, s, a))
	}
	boom := errors.New("rename failed")
	if err := s.Admit(ctx, Charge{ProjectID: p, Kind: "chat", SessionID: a, Bytes: 5}, capOf(100, 5),
		func() error { return boom }); !errors.Is(err, boom) || held(t, s, a) != 0 {
		t.Fatalf("a failed commit is not charged: %v, a %d", err, held(t, s, a))
	}
	ran := false
	commit := func() error { ran = true; return nil }
	if err := s.Admit(ctx, Charge{ProjectID: "p_gone_1", Kind: "chat", SessionID: a, Bytes: 5}, capOf(100, 5), commit); !errors.Is(err, ErrNotFound) || ran {
		t.Fatalf("a vanished project: %v (commit ran: %v), want ErrNotFound first", err, ran)
	}
	if err := s.Admit(ctx, Charge{ProjectID: p, Kind: "chat", SessionID: "s_gone_1", Bytes: 5}, capOf(100, 5), commit); err == nil || ran {
		t.Fatalf("an unknown session: %v (commit ran: %v), want a refusal", err, ran)
	}
}

// The S-3 session sweep drops a swept session's charges; the bytes stay, charged to nobody.
func TestSweptSessionDropsItsCharges(t *testing.T) {
	s, session, project := chargeRig(t)
	ctx := context.Background()
	a, keep := session(), session()
	p := project(keep)
	for sess, kind := range map[string]string{a: protocol.KindOriginal, keep: protocol.KindResult} {
		if err := s.Admit(ctx, Charge{ProjectID: p, Kind: kind, SessionID: sess, Bytes: 10}, capOf(100, 10), func() error { return nil }); err != nil {
			t.Fatal(err)
		}
	}
	if _, err := s.pool.Exec(ctx, `UPDATE sessions SET expires_at = 5 WHERE id = $1`, a); err != nil {
		t.Fatal(err)
	}
	if n, err := s.DeleteExpiredSessions(ctx, 10, 0); err != nil || n != 1 {
		t.Fatalf("sweep: %d %v", n, err)
	}
	if held(t, s, a) != 0 || held(t, s, keep) != 10 {
		t.Fatalf("after the sweep: swept %d, kept %d", held(t, s, a), held(t, s, keep))
	}
	if n, err := s.ClearCharges(ctx); err != nil || n != 1 || held(t, s, keep) != 0 {
		t.Fatalf("clear: %d %v, kept %d", n, err, held(t, s, keep))
	}
}

// Parallel uploads by one session into eight projects: the session row lock checks them one at a time,
// so exactly as many fit as the cap allows.
func TestAdmitSerialisesOneSessionsUploads(t *testing.T) {
	s, session, project := chargeRig(t)
	ctx := context.Background()
	a := session()
	var ids []string
	for i := 0; i < 8; i++ {
		ids = append(ids, project(a))
	}
	var (
		wg        sync.WaitGroup
		mu        sync.Mutex
		committed int
	)
	for _, id := range ids {
		wg.Add(1)
		go func(id string) {
			defer wg.Done()
			err := s.Admit(ctx, Charge{ProjectID: id, Kind: protocol.KindOriginal, SessionID: a, Bytes: 30}, capOf(100, 30),
				func() error { mu.Lock(); committed++; mu.Unlock(); return nil })
			if err != nil && !errors.Is(err, errOver) {
				t.Error(err)
			}
		}(id)
	}
	wg.Wait()
	if committed != 3 || held(t, s, a) != 90 {
		t.Fatalf("%d uploads of 30 B committed under a 100 B cap (held %d), want exactly 3", committed, held(t, s, a))
	}
	if n := strconv.FormatInt(held(t, s, "s_nobody_1"), 10); n != "0" {
		t.Fatalf("a session with no charges holds %s", n)
	}
}
