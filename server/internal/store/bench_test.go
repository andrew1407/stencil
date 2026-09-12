package store

// Opt-in and DB-gated: `TEST_DATABASE_URL=... go test -bench . ./internal/store/`
// (CI never passes -bench; without the URL requireStore skips). Baselines are in
// server/README.md#benchmarks.

import (
	"context"
	"fmt"
	"strconv"
	"strings"
	"testing"

	"stencil/server/internal/protocol"
)

// seedProjects inserts n projects with a layout and an inline original, so a
// SELECT * would carry the payload columns ListProjects deliberately omits.
func seedProjects(b *testing.B, s *Store, n int) {
	b.Helper()
	ctx := context.Background()
	layout := []byte(`{"rows":[{"cells":[{"w":1,"h":1}]}]}`)
	blob := strings.Repeat("data:image/png;base64,iVBORw0KGgo=", 2000) // text column: no NULs
	for i := 0; i < n; i++ {
		if _, err := s.CreateProject(ctx, "", protocol.CreateProjectRequest{
			Name: "P" + strconv.Itoa(i), HasImage: true, Layout: layout,
			OriginalContent: blob, Keywords: []string{"maps", "ocean"},
		}); err != nil {
			b.Fatal(err)
		}
	}
}

// BenchmarkListProjects measures the projects listing at a realistic corpus
// size, whole and one keyset page at a time. The gap between them is what the
// `?limit=&after=` pagination buys a client that only shows the first screen.
func BenchmarkListProjects(b *testing.B) {
	s := requireStore(b)
	ctx := context.Background()
	const corpus = 500
	seedProjects(b, s, corpus)

	for _, limit := range []int{0, 50} {
		name := "whole"
		if limit > 0 {
			name = fmt.Sprintf("page=%d", limit)
		}
		b.Run(name, func(b *testing.B) {
			b.ReportAllocs()
			for i := 0; i < b.N; i++ {
				out, err := s.ListProjects(ctx, ProjectPage{Limit: limit})
				if err != nil {
					b.Fatal(err)
				}
				if limit == 0 && len(out) != corpus {
					b.Fatalf("listed %d of %d", len(out), corpus)
				}
			}
		})
	}
}
