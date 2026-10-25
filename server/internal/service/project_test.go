package service

// The project rules, driven straight against the service so they hold on any path.

import (
	"context"
	"errors"
	"strings"
	"testing"
	"time"

	"stencil/server/internal/clock"
	"stencil/server/internal/eventbus"
	"stencil/server/internal/protocol"
	"stencil/server/internal/store"
	"stencil/server/internal/testutil"
)

func projectSvc(t *testing.T, ttl time.Duration) (*ProjectService, *testutil.MemStore, *fakeFiles) {
	t.Helper()
	st, files := testutil.NewMemStore(), &fakeFiles{}
	return NewProjects(st, files, eventbus.NewInProc(), ttl), st, files
}

// A Stencil project is created FROM an image; bare metadata is refused before
// any row exists.
func TestCreateRejectsAnImagelessProject(t *testing.T) {
	svc, st, _ := projectSvc(t, 0)
	if _, err := svc.Create(context.Background(), "", protocol.CreateProjectRequest{Name: "bare"}); !errors.Is(err, ErrImageRequired) {
		t.Fatalf("create without an image: %v, want ErrImageRequired", err)
	}
	if list, _ := st.ListProjects(context.Background(), listAll); len(list) != 0 {
		t.Fatalf("a refused create still wrote %d rows", len(list))
	}
}

// PROJECT_TTL stamps an expiry only when the client named none.
func TestCreateStampsTheDefaultTTL(t *testing.T) {
	t.Cleanup(clock.Stub(clock.Fixed(1_000)))
	svc, _, _ := projectSvc(t, time.Hour)
	ctx := context.Background()

	auto, err := svc.Create(ctx, "", protocol.CreateProjectRequest{Name: "auto", HasImage: true})
	if err != nil {
		t.Fatal(err)
	}
	if want := int64(1_000 + time.Hour.Milliseconds()); auto.ExpiresAt != want {
		t.Fatalf("expiresAt = %d, want %d", auto.ExpiresAt, want)
	}
	pinned, err := svc.Create(ctx, "", protocol.CreateProjectRequest{Name: "pinned", HasImage: true, ExpiresAt: 42})
	if err != nil {
		t.Fatal(err)
	}
	if pinned.ExpiresAt != 42 {
		t.Fatalf("an explicit expiry was overwritten: %d", pinned.ExpiresAt)
	}
}

// No TTL configured means no expiry at all.
func TestCreateLeavesExpiryUnsetWithoutATTL(t *testing.T) {
	svc, _, _ := projectSvc(t, 0)
	rec, err := svc.Create(context.Background(), "", protocol.CreateProjectRequest{Name: "n", HasImage: true})
	if err != nil {
		t.Fatal(err)
	}
	if rec.ExpiresAt != 0 {
		t.Fatalf("expiresAt = %d, want 0", rec.ExpiresAt)
	}
}

// A delete takes the row first, then the bytes.
func TestDeleteDropsRowThenBytes(t *testing.T) {
	svc, st, files := projectSvc(t, 0)
	st.Seed(protocol.ProjectRecord{ID: "p_1"})
	if err := svc.Delete(context.Background(), "p_1"); err != nil {
		t.Fatal(err)
	}
	if _, ok := st.Project("p_1"); ok {
		t.Fatal("the row survived the delete")
	}
	if len(files.removed) != 1 || files.removed[0] != "p_1" {
		t.Fatalf("removed = %v, want [p_1]", files.removed)
	}
}

// A delete of an id that names no row is ErrNotFound and drops and announces nothing: a forged id
// must not detach every editor of a real project.
func TestDeleteOfAMissingIDAnnouncesNothing(t *testing.T) {
	svc, _, files := projectSvc(t, 0)
	events, unsub := svc.Bus.Subscribe(eventbus.ChannelEvents)
	t.Cleanup(unsub)
	if err := svc.Delete(context.Background(), "p_nope"); !errors.Is(err, store.ErrNotFound) {
		t.Fatalf("delete of a missing id = %v, want ErrNotFound", err)
	}
	if len(files.removed) != 0 {
		t.Fatalf("removed = %v, want nothing", files.removed)
	}
	select {
	case env := <-events:
		t.Fatalf("a missing id was announced: %s", env.Data)
	case <-time.After(50 * time.Millisecond):
	}
}

// Dropped is the sweep's half: it takes bytes and announces, never a row.
func TestDroppedTakesBytesWithoutTouchingTheRow(t *testing.T) {
	svc, st, files := projectSvc(t, 0)
	st.Seed(protocol.ProjectRecord{ID: "p_9"})
	svc.Dropped(context.Background(), "p_9")
	if len(files.removed) != 1 {
		t.Fatalf("removed = %v, want one id", files.removed)
	}
	if _, ok := st.Project("p_9"); !ok {
		t.Fatal("Dropped deleted the row; the sweep has already done that")
	}
}

// An update writes under its expected version and announces the new record; a stale one announces nothing.
func TestUpdateAnnouncesOnlyAWrittenVersion(t *testing.T) {
	svc, st, _ := projectSvc(t, 0)
	st.Seed(protocol.ProjectRecord{ID: "p_1", Version: 1})
	events, unsub := svc.Bus.Subscribe(eventbus.ChannelEvents)
	t.Cleanup(unsub)
	name := "renamed"
	if _, err := svc.Update(context.Background(), "p_1", protocol.UpdateProjectRequest{Name: &name, Version: 9}); !errors.Is(err, store.ErrConflict) {
		t.Fatalf("stale update = %v, want ErrConflict", err)
	}
	rec, err := svc.Update(context.Background(), "p_1", protocol.UpdateProjectRequest{Name: &name, Version: 1})
	if err != nil || rec.Name != name || rec.Version != 2 {
		t.Fatalf("update = %+v, %v; want the renamed record at version 2", rec, err)
	}
	if env := <-events; !strings.Contains(string(env.Data), `"updated"`) || !strings.Contains(string(env.Data), name) {
		t.Fatalf("event %s, want the one updated announcement", env.Data)
	}
	select {
	case env := <-events:
		t.Fatalf("a stale update announced: %s", env.Data)
	default:
	}
}
