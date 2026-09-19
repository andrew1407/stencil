package store

// DB-gated: the ProjectPatch columns that must tell "absent" from "cleared" —
// a nil pointer leaves the column alone, an empty value clears it.

import (
	"context"
	"encoding/json"
	"testing"

	"stencil/server/internal/protocol"
)

// TestUpdateProjectColor exercises the COALESCE color path: a non-nil pointer
// sets the colour, nil leaves it untouched, and an empty string clears it.
func TestUpdateProjectColor(t *testing.T) {
	s := requireStore(t)
	ctx := context.Background()
	p, _ := s.CreateProject(ctx, "", protocol.CreateProjectRequest{Name: "C", Color: "#112233"})

	// Set a new colour, leave name/layout untouched (nil).
	red := "#ff0000"
	upd, err := s.UpdateProject(ctx, p.ID, ProjectPatch{Color: &red}, p.Version)
	if err != nil {
		t.Fatalf("update color: %v", err)
	}
	if upd.Color != "#ff0000" || upd.Name != "C" {
		t.Fatalf("color update changed wrong fields: %+v", upd)
	}

	// nil color preserves the value while bumping version via a name change.
	name := "C2"
	upd, err = s.UpdateProject(ctx, p.ID, ProjectPatch{Name: &name}, upd.Version)
	if err != nil {
		t.Fatalf("update name: %v", err)
	}
	if upd.Color != "#ff0000" || upd.Name != "C2" {
		t.Fatalf("nil color should be preserved: %+v", upd)
	}

	// Empty string explicitly clears the colour (theme fallback).
	empty := ""
	upd, err = s.UpdateProject(ctx, p.ID, ProjectPatch{Color: &empty}, upd.Version)
	if err != nil {
		t.Fatalf("clear color: %v", err)
	}
	if upd.Color != "" {
		t.Fatalf("empty color should clear, got %q", upd.Color)
	}
}

// The COALESCE keywords path: create carries keywords, a non-nil slice sets them (normalized/deduped),
// nil leaves them untouched, and an empty slice clears them.
func TestUpdateProjectKeywords(t *testing.T) {
	s := requireStore(t)
	ctx := context.Background()
	p, _ := s.CreateProject(ctx, "", protocol.CreateProjectRequest{Name: "K", Keywords: []string{"alpha", "Beta", "alpha"}})
	// Create normalizes: dedupe (case-insensitive), preserve first-seen order.
	if len(p.Keywords) != 2 || p.Keywords[0] != "alpha" || p.Keywords[1] != "Beta" {
		t.Fatalf("create keywords not normalized: %+v", p.Keywords)
	}

	// Set new keywords, leave name untouched (nil).
	kw := []string{"gamma", "delta"}
	upd, err := s.UpdateProject(ctx, p.ID, ProjectPatch{Keywords: &kw}, p.Version)
	if err != nil {
		t.Fatalf("update keywords: %v", err)
	}
	if len(upd.Keywords) != 2 || upd.Keywords[0] != "gamma" || upd.Name != "K" {
		t.Fatalf("keywords update changed wrong fields: %+v", upd)
	}

	// nil keywords preserves the value while bumping version via a name change.
	name := "K2"
	upd, err = s.UpdateProject(ctx, p.ID, ProjectPatch{Name: &name}, upd.Version)
	if err != nil {
		t.Fatalf("update name: %v", err)
	}
	if len(upd.Keywords) != 2 || upd.Name != "K2" {
		t.Fatalf("nil keywords should be preserved: %+v", upd)
	}

	// Empty slice explicitly clears the keywords.
	empty := make([]string, 0)
	upd, err = s.UpdateProject(ctx, p.ID, ProjectPatch{Keywords: &empty}, upd.Version)
	if err != nil {
		t.Fatalf("clear keywords: %v", err)
	}
	if len(upd.Keywords) != 0 {
		t.Fatalf("empty keywords should clear, got %+v", upd.Keywords)
	}
}

// The COALESCE blank_color path: create carries the fill, Blank is derived (non-empty ⇔ true), a non-nil
// pointer sets it, nil leaves it untouched, and "" clears it.
func TestUpdateProjectBlankColor(t *testing.T) {
	s := requireStore(t)
	ctx := context.Background()
	p, _ := s.CreateProject(ctx, "", protocol.CreateProjectRequest{Name: "B", BlankColor: "#00aaff"})
	if p.BlankColor != "#00aaff" || !p.Blank {
		t.Fatalf("create blank colour/flag wrong: %q blank=%v", p.BlankColor, p.Blank)
	}

	// Recolour, leave name untouched (nil).
	next := "#ff0000"
	upd, err := s.UpdateProject(ctx, p.ID, ProjectPatch{BlankColor: &next}, p.Version)
	if err != nil {
		t.Fatalf("update blank colour: %v", err)
	}
	if upd.BlankColor != "#ff0000" || !upd.Blank || upd.Name != "B" {
		t.Fatalf("blank colour update changed wrong fields: %+v", upd)
	}

	// nil blank colour is preserved while a name change bumps the version.
	name := "B2"
	upd, err = s.UpdateProject(ctx, p.ID, ProjectPatch{Name: &name}, upd.Version)
	if err != nil {
		t.Fatalf("update name: %v", err)
	}
	if upd.BlankColor != "#ff0000" || upd.Name != "B2" {
		t.Fatalf("nil blank colour should be preserved: %+v", upd)
	}

	// "" clears it → no longer a blank project (Blank derived false).
	clear := ""
	upd, err = s.UpdateProject(ctx, p.ID, ProjectPatch{BlankColor: &clear}, upd.Version)
	if err != nil {
		t.Fatalf("clear blank colour: %v", err)
	}
	if upd.BlankColor != "" || upd.Blank {
		t.Fatalf("empty blank colour should clear + derive blank=false, got %q blank=%v", upd.BlankColor, upd.Blank)
	}
}

// sameJSON reports whether two JSON payloads are semantically equal, ignoring
// whitespace and key ordering (jsonb does not preserve either).
func sameJSON(t *testing.T, a, b []byte) bool {
	t.Helper()
	var av, bv any
	if err := json.Unmarshal(a, &av); err != nil {
		t.Fatalf("unmarshal a: %v", err)
	}
	if err := json.Unmarshal(b, &bv); err != nil {
		t.Fatalf("unmarshal b: %v", err)
	}
	am, _ := json.Marshal(av)
	bm, _ := json.Marshal(bv)
	return string(am) == string(bm)
}
