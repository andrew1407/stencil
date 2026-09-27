package store_test

// DB-gated: the project_presence heartbeat, and the sweep and delete guard reading it across instances.

import (
	"context"
	"errors"
	"slices"
	"testing"
	"time"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/protocol"
	"stencil/server/internal/service"
	"stencil/server/internal/store"
)

type liveCounts map[string]int

func (l liveCounts) LiveCounts() map[string]int { return l }

func elsewhere(t *testing.T, s *store.Store, instance string) []string {
	t.Helper()
	ids, err := s.LiveElsewhere(context.Background(), instance)
	if err != nil {
		t.Fatal(err)
	}
	slices.Sort(ids)
	return ids
}

func members(t *testing.T, s *store.Store, instance, id string) int {
	t.Helper()
	n, err := s.MembersElsewhere(context.Background(), instance, id)
	if err != nil {
		t.Fatal(err)
	}
	return n
}

func beat(t *testing.T, s *store.Store, instance string, live map[string]int, ttl time.Duration) {
	t.Helper()
	if err := s.BeatPresence(context.Background(), instance, live, ttl); err != nil {
		t.Fatal(err)
	}
}

// A beat replaces the instance's rows; the others see them, the instance itself never does.
func TestPresenceBeatReplacesTheInstanceRows(t *testing.T) {
	s := store.RequireTestStore(t)
	beat(t, s, "i_a", map[string]int{"p_1": 2, "p_2": 1}, time.Minute)
	if got := elsewhere(t, s, "i_b"); !slices.Equal(got, []string{"p_1", "p_2"}) {
		t.Fatalf("another instance sees %v", got)
	}
	if n := members(t, s, "i_b", "p_1"); n != 2 {
		t.Fatalf("p_1 has %d editors elsewhere, want 2", n)
	}
	if got := elsewhere(t, s, "i_a"); len(got) != 0 {
		t.Fatalf("an instance counts its own rows: %v", got)
	}

	beat(t, s, "i_a", map[string]int{"p_2": 3}, time.Minute)
	if got := elsewhere(t, s, "i_b"); !slices.Equal(got, []string{"p_2"}) || members(t, s, "i_b", "p_2") != 3 {
		t.Fatalf("after a close, another instance sees %v", got)
	}
	beat(t, s, "i_a", map[string]int{}, time.Minute)
	if got := elsewhere(t, s, "i_b"); len(got) != 0 {
		t.Fatalf("an instance with no sessions still publishes %v", got)
	}
}

// Editors sum across instances; a lapsed row counts for nothing and, lapsed a further TTL, is purged
// by the next instance to beat.
func TestPresenceSumsInstancesAndLapses(t *testing.T) {
	s := store.RequireTestStore(t)
	beat(t, s, "i_a", map[string]int{"p_1": 1}, time.Millisecond)
	beat(t, s, "i_b", map[string]int{"p_1": 2}, time.Minute)
	time.Sleep(20 * time.Millisecond)
	if n := members(t, s, "i_c", "p_1"); n != 2 {
		t.Fatalf("p_1 has %d live editors elsewhere, want i_b's 2 (i_a's row lapsed)", n)
	}

	var rows int
	count := func() int {
		if err := s.MigratePool().QueryRow(context.Background(), `SELECT count(*) FROM project_presence`).Scan(&rows); err != nil {
			t.Fatal(err)
		}
		return rows
	}
	beat(t, s, "i_c", map[string]int{}, time.Hour)
	if count() != 2 {
		t.Fatalf("%d rows: a row lapsed less than a TTL ago was purged", rows)
	}
	beat(t, s, "i_c", map[string]int{}, time.Millisecond)
	if count() != 1 {
		t.Fatalf("%d rows: i_a's long-lapsed row survived", rows)
	}
}

// Two instances on one database: an expired project edited on one is spared by the other's sweep, and
// the sweeping instance's own presence rows never stand in for its exact in-memory list.
func TestExpirySweepSparesAProjectLiveOnAnotherInstance(t *testing.T) {
	s := store.RequireTestStore(t)
	ctx := context.Background()
	busy, _ := s.CreateProject(ctx, "", protocol.CreateProjectRequest{Name: "Busy", ExpiresAt: 1_000})
	idle, _ := s.CreateProject(ctx, "", protocol.CreateProjectRequest{Name: "Idle", ExpiresAt: 1_000})

	a, _ := service.NewPresence(s, liveCounts{busy.ID: 1}, time.Minute)
	b, _ := service.NewPresence(s, liveCounts{}, time.Minute)
	if err := a.Beat(ctx); err != nil {
		t.Fatal(err)
	}
	ids, err := service.Expiry{Store: s, Remote: b}.DeleteExpiredProjects(ctx, 5_000, 10)
	if err != nil || !slices.Equal(ids, []string{idle.ID}) {
		t.Fatalf("instance b swept %v (%v), want only the idle project", ids, err)
	}
	if ids, _ := (service.Expiry{Store: s, Remote: a}).DeleteExpiredProjects(ctx, 5_000, 10); !slices.Equal(ids, []string{busy.ID}) {
		t.Fatalf("with its session gone, instance a's own row still spared the project: %v", ids)
	}
}

// DELETE's guard on one instance counts the editors another instance published.
func TestDeleteGuardCountsAnotherInstancesEditors(t *testing.T) {
	s := store.RequireTestStore(t)
	ctx := context.Background()
	p, _ := s.CreateProject(ctx, "", protocol.CreateProjectRequest{Name: "Shared"})
	a, _ := service.NewPresence(s, liveCounts{p.ID: 2}, time.Minute)
	b, _ := service.NewPresence(s, liveCounts{}, time.Minute)
	if err := a.Beat(ctx); err != nil {
		t.Fatal(err)
	}
	svc := service.NewProjects(s, nil, nil, eventbus.NewInProc(), 0)
	svc.Remote = b
	if err := svc.Delete(ctx, p.ID); !errors.Is(err, service.ErrProjectInUse) {
		t.Fatalf("delete of a project two editors share elsewhere: %v, want ErrProjectInUse", err)
	}
	a.Live = liveCounts{p.ID: 1}
	if err := a.Beat(ctx); err != nil {
		t.Fatal(err)
	}
	if err := svc.Delete(ctx, p.ID); err != nil {
		t.Fatalf("the lone editor elsewhere leaves the project deletable: %v", err)
	}
}
