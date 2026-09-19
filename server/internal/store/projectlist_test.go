package store

import (
	"context"
	"strings"
	"testing"

	"stencil/server/internal/clock"
	"stencil/server/internal/protocol"
)

// The list query must not select the two payload columns. It runs without a database — it reads the SQL
// the list path is built from.
func TestProjectListColsCarryNoPayload(t *testing.T) {
	for _, col := range []string{"original_content", "layout"} {
		if strings.Contains(projectListCols, col) {
			t.Errorf("projectListCols must not select %s", col)
		}
		if !strings.Contains(projectCols, col) {
			t.Errorf("projectCols should still select %s", col)
		}
	}
	full, list := splitCols(projectCols), splitCols(projectListCols)
	if len(full) != len(list)+2 {
		t.Fatalf("list has %d columns, full has %d; want exactly two fewer", len(list), len(full))
	}
	// Same order, so one scanner can read both (scanProject skips the pair).
	var want []string
	for _, c := range full {
		if c != "original_content" && c != "layout" {
			want = append(want, c)
		}
	}
	if strings.Join(want, ",") != strings.Join(list, ",") {
		t.Fatalf("column order diverged:\n full: %v\n list: %v", want, list)
	}
}

func splitCols(cols string) []string {
	var out []string
	for _, c := range strings.Split(cols, ",") {
		if c = strings.TrimSpace(strings.ReplaceAll(c, "\n", "")); c != "" {
			out = append(out, c)
		}
	}
	return out
}

func TestProjectCursorRoundTrip(t *testing.T) {
	c := ProjectCursor{UpdatedAt: 1700000000123, ID: "p_abc_de"}
	got, err := ParseProjectCursor(c.String())
	if err != nil || got != c {
		t.Fatalf("round trip: %v %+v", err, got)
	}
	if empty, err := ParseProjectCursor(""); err != nil || empty != (ProjectCursor{}) {
		t.Fatalf("empty cursor: %v %+v", err, empty)
	}
	if (ProjectCursor{}).String() != "" {
		t.Fatal("the zero cursor must render empty")
	}
	for _, bad := range []string{"nonsense", "12", "12.", ".p_x", "x.p_y"} {
		if _, err := ParseProjectCursor(bad); err == nil {
			t.Errorf("cursor %q should be rejected", bad)
		}
	}
}

// DB-gated (TEST_DATABASE_URL): the list path returns no payload while GetProject
// still does.
func TestListProjectsOmitsPayload(t *testing.T) {
	s := requireStore(t)
	ctx := context.Background()
	p, err := s.CreateProject(ctx, "", protocol.CreateProjectRequest{
		Name: "Heavy", OriginalContent: strings.Repeat("A", 4096), Layout: []byte(`{"lines":[]}`)})
	if err != nil {
		t.Fatal(err)
	}
	list, err := s.ListProjects(ctx, ProjectPage{})
	if err != nil || len(list) != 1 {
		t.Fatalf("list: %v %d rows", err, len(list))
	}
	if list[0].OriginalContent != "" || list[0].Layout != nil {
		t.Fatalf("list row carried a payload: content=%d layout=%d",
			len(list[0].OriginalContent), len(list[0].Layout))
	}
	full, err := s.GetProject(ctx, p.ID)
	if err != nil || len(full.OriginalContent) != 4096 || full.Layout == nil {
		t.Fatalf("get should still carry the payload: %v %+v", err, full)
	}
}

// DB-gated: keyset paging walks every project exactly once, newest first.
func TestListProjectsKeysetPaging(t *testing.T) {
	s := requireStore(t)
	ctx := context.Background()
	for i := 0; i < 5; i++ {
		if _, err := s.CreateProject(ctx, "", protocol.CreateProjectRequest{Name: "P"}); err != nil {
			t.Fatal(err)
		}
	}
	// The unpaged list is the reference order (creates inside one millisecond tie
	// on updated_at, so the id tiebreak — not creation order — decides).
	all, err := s.ListProjects(ctx, ProjectPage{})
	if err != nil || len(all) != 5 {
		t.Fatalf("unpaged list: %v %d rows", err, len(all))
	}
	want := ids(all)
	var got []string
	page := ProjectPage{Limit: 2}
	for {
		rows, err := s.ListProjects(ctx, page)
		if err != nil {
			t.Fatal(err)
		}
		got = append(got, ids(rows)...)
		if len(rows) < page.Limit {
			break
		}
		last := rows[len(rows)-1]
		page.After = ProjectCursor{UpdatedAt: last.UpdatedAt, ID: last.ID}
	}
	// Same rows, same order, each exactly once.
	if strings.Join(got, ",") != strings.Join(want, ",") {
		t.Fatalf("paged walk %v, want %v", got, want)
	}
}

// DB-gated: newest-updated first, with a total order — rows sharing a millisecond
// are broken by id, so a keyset page can neither skip nor repeat one.
func TestListProjectsOrderIsTotal(t *testing.T) {
	s := requireStore(t)
	ctx := context.Background()
	t.Cleanup(clock.Stub(clock.Fixed(1_000)))
	older, err := s.CreateProject(ctx, "", protocol.CreateProjectRequest{Name: "Older"})
	if err != nil {
		t.Fatal(err)
	}
	clock.NowMs = clock.Fixed(2_000)
	newer, err := s.CreateProject(ctx, "", protocol.CreateProjectRequest{Name: "Newer"})
	if err != nil {
		t.Fatal(err)
	}
	list, err := s.ListProjects(ctx, ProjectPage{})
	if err != nil {
		t.Fatal(err)
	}
	if len(list) != 2 || list[0].ID != newer.ID || list[1].ID != older.ID {
		t.Fatalf("not newest-updated first: %v", ids(list))
	}

	// A third row in the same millisecond as `newer`: the id decides, descending.
	twin, err := s.CreateProject(ctx, "", protocol.CreateProjectRequest{Name: "Twin"})
	if err != nil {
		t.Fatal(err)
	}
	hi, lo := newer.ID, twin.ID
	if lo > hi {
		hi, lo = lo, hi
	}
	list, _ = s.ListProjects(ctx, ProjectPage{})
	if len(list) != 3 || list[0].ID != hi || list[1].ID != lo {
		t.Fatalf("tie not broken by id desc: %v", ids(list))
	}
}

func ids(list []protocol.ProjectRecord) []string {
	var out []string
	for _, r := range list {
		out = append(out, r.ID)
	}
	return out
}
