package service

// The two project rules the transports used to each own a copy of. Driven
// straight against the service, so they hold on any path — REST today, the WS
// hub tomorrow.

import (
	"context"
	"errors"
	"testing"
	"time"

	"stencil/server/internal/clock"
	"stencil/server/internal/eventbus"
	"stencil/server/internal/protocol"
	"stencil/server/internal/testutil"
)

func projectSvc(t *testing.T, live SessionCounter, ttl time.Duration) (*ProjectService, *testutil.MemStore, *fakeFiles) {
	t.Helper()
	st, files := testutil.NewMemStore(), &fakeFiles{}
	return NewProjects(st, files, live, eventbus.NewInProc(), ttl), st, files
}

// A Stencil project is created FROM an image; bare metadata is refused before
// any row exists.
func TestCreateRejectsAnImagelessProject(t *testing.T) {
	svc, st, _ := projectSvc(t, nil, 0)
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
	svc, _, _ := projectSvc(t, nil, time.Hour)
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
	svc, _, _ := projectSvc(t, nil, 0)
	rec, err := svc.Create(context.Background(), "", protocol.CreateProjectRequest{Name: "n", HasImage: true})
	if err != nil {
		t.Fatal(err)
	}
	if rec.ExpiresAt != 0 {
		t.Fatalf("expiresAt = %d, want 0", rec.ExpiresAt)
	}
}

// Deletion is refused while two or more clients share the live edit session, and
// nothing is dropped when it is.
func TestDeleteRefusesWhileTheSessionIsShared(t *testing.T) {
	svc, st, files := projectSvc(t, fakeCounter(2), 0)
	st.Seed(protocol.ProjectRecord{ID: "p_1"})
	if err := svc.Delete(context.Background(), "p_1"); !errors.Is(err, ErrProjectInUse) {
		t.Fatalf("delete with 2 peers: %v, want ErrProjectInUse", err)
	}
	if _, ok := st.Project("p_1"); !ok {
		t.Fatal("a refused delete removed the row")
	}
	if len(files.removed) != 0 {
		t.Fatalf("a refused delete dropped bytes: %v", files.removed)
	}
}

// One editor (or none) may delete; the row goes first, then the bytes.
func TestDeleteDropsRowThenBytes(t *testing.T) {
	svc, st, files := projectSvc(t, fakeCounter(1), 0)
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

// Dropped is the sweep's half: it takes bytes and announces, never a row.
func TestDroppedTakesBytesWithoutTouchingTheRow(t *testing.T) {
	svc, st, files := projectSvc(t, nil, 0)
	st.Seed(protocol.ProjectRecord{ID: "p_9"})
	svc.Dropped(context.Background(), "p_9")
	if len(files.removed) != 1 {
		t.Fatalf("removed = %v, want one id", files.removed)
	}
	if _, ok := st.Project("p_9"); !ok {
		t.Fatal("Dropped deleted the row; the sweep has already done that")
	}
}
