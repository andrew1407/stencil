package service

import (
	"context"
	"errors"
	"maps"
	"strings"
	"testing"
	"time"

	"stencil/server/internal/protocol"
	"stencil/server/internal/testutil"
)

// fakePresence is the shared table as one other instance sees it: what it last published, and what
// the rest publish.
type fakePresence struct {
	instance  string
	published map[string]int
	ttl       time.Duration
	elsewhere map[string]int
	err       error
}

func (f *fakePresence) BeatPresence(_ context.Context, instance string, live map[string]int, ttl time.Duration) error {
	f.instance, f.published, f.ttl = instance, live, ttl
	return f.err
}

func (f *fakePresence) LiveElsewhere(context.Context, string) ([]string, error) {
	ids := make([]string, 0, len(f.elsewhere))
	for id := range f.elsewhere {
		ids = append(ids, id)
	}
	return ids, f.err
}

func (f *fakePresence) MembersElsewhere(_ context.Context, _, id string) (int, error) {
	return f.elsewhere[id], f.err
}

type liveCounts map[string]int

func (l liveCounts) LiveCounts() map[string]int { return l }

// A beat publishes the hub's counts under this instance's own id, with the configured TTL.
func TestPresenceBeatPublishesTheLiveCounts(t *testing.T) {
	st := &fakePresence{}
	p, err := NewPresence(st, liveCounts{"p_a": 2, "p_b": 1}, time.Minute)
	if err != nil {
		t.Fatal(err)
	}
	if err := p.Beat(context.Background()); err != nil {
		t.Fatal(err)
	}
	if st.instance != p.Instance || st.ttl != time.Minute || !maps.Equal(st.published, map[string]int{"p_a": 2, "p_b": 1}) {
		t.Fatalf("published %v as %q for %v", st.published, st.instance, st.ttl)
	}
	other, _ := NewPresence(st, liveCounts{}, time.Minute)
	if !strings.HasPrefix(p.Instance, "i_") || other.Instance == p.Instance {
		t.Fatalf("instance ids %q and %q must be distinct and prefixed", p.Instance, other.Instance)
	}
}

// An expired project live on another instance is spared like one live here; if the other instances'
// list cannot be read, nothing is swept.
func TestExpiryDefersProjectsLiveElsewhere(t *testing.T) {
	st := testutil.NewMemStore()
	for _, id := range []string{"p_old_a", "p_old_b", "p_old_c"} {
		st.Seed(protocol.ProjectRecord{ID: id, ExpiresAt: 10})
	}
	remote := &fakePresence{elsewhere: map[string]int{"p_old_c": 1}}
	e := Expiry{Store: st, Live: liveIDs{"p_old_b"}, Remote: &Presence{Store: remote}}

	remote.err = errors.New("db down")
	if ids, err := e.DeleteExpiredProjects(context.Background(), 100, 10); err == nil || len(ids) != 0 {
		t.Fatalf("an unreadable remote list swept %v (%v)", ids, err)
	}
	remote.err = nil
	ids, err := e.DeleteExpiredProjects(context.Background(), 100, 10)
	if err != nil || len(ids) != 1 || ids[0] != "p_old_a" {
		t.Fatalf("swept %v (%v), want only the project idle everywhere", ids, err)
	}
	for _, id := range []string{"p_old_b", "p_old_c"} {
		if _, ok := st.Project(id); !ok {
			t.Fatalf("%s is being edited and was swept", id)
		}
	}
}

// The delete guard counts editors on every instance: one here and one elsewhere is a shared session.
func TestDeleteCountsEditorsOnOtherInstances(t *testing.T) {
	remote := &fakePresence{elsewhere: map[string]int{"p_1": 1}}
	svc, st, files := projectSvc(t, fakeCounter(1), 0)
	svc.Remote = &Presence{Store: remote}
	st.Seed(protocol.ProjectRecord{ID: "p_1"})
	if err := svc.Delete(context.Background(), "p_1"); !errors.Is(err, ErrProjectInUse) {
		t.Fatalf("one editor here and one elsewhere: %v, want ErrProjectInUse", err)
	}

	remote.err = errors.New("db down")
	svc.Live = fakeCounter(0)
	if err := svc.Delete(context.Background(), "p_1"); err == nil || errors.Is(err, ErrProjectInUse) {
		t.Fatalf("an unreadable remote count must fail the delete, got %v", err)
	}
	if _, ok := st.Project("p_1"); !ok || len(files.removed) != 0 {
		t.Fatal("a refused delete removed the project")
	}

	remote.err = nil
	if err := svc.Delete(context.Background(), "p_1"); err != nil {
		t.Fatalf("the lone editor, on another instance, may delete: %v", err)
	}
}
