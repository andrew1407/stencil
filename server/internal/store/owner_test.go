package store

// DB-gated: the owner query behind STORAGE_QUOTA_PER_OWNER_BYTES.

import (
	"context"
	"sort"
	"strings"
	"testing"

	"stencil/server/internal/auth"
	"stencil/server/internal/protocol"
)

func TestOwnerProjectIDs(t *testing.T) {
	s := requireStore(t)
	ctx := context.Background()
	session := func() string {
		_, hash, _ := auth.GenerateToken()
		sess, err := s.CreateSession(ctx, hash, "cli", 1000, 0)
		if err != nil {
			t.Fatal(err)
		}
		return sess.ID
	}
	a, b := session(), session()
	create := func(owner string) string {
		p, err := s.CreateProject(ctx, owner, protocol.CreateProjectRequest{Name: "O"})
		if err != nil {
			t.Fatal(err)
		}
		return p.ID
	}
	a1, a2, b1, none := create(a), create(a), create(b), create("")
	ids := func(id string) string {
		got, err := s.OwnerProjectIDs(ctx, id)
		if err != nil {
			t.Fatal(err)
		}
		sort.Strings(got)
		return strings.Join(got, ",")
	}
	want := []string{a1, a2}
	sort.Strings(want)
	if got := ids(a2); got != strings.Join(want, ",") {
		t.Fatalf("owner a's projects = %s, want %v", got, want)
	}
	if got := ids(b1); got != b1 {
		t.Fatalf("owner b's projects = %s, want %s", got, b1)
	}
	if got := ids(none); got != "" {
		t.Fatalf("an ownerless project named peers %s", got)
	}
	if got := ids("p_missing_1"); got != "" {
		t.Fatalf("a missing project named peers %s", got)
	}
}
