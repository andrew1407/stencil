package protocol

// The file-kind allowlist: what the files endpoints and the filestore accept,
// and which kinds the per-file DELETE route may remove.

import (
	"reflect"
	"testing"
)

func TestIsVariantKindAcceptsExactlyOneThroughEight(t *testing.T) {
	for _, kind := range []string{"variant1", "variant2", "variant3", "variant4",
		"variant5", "variant6", "variant7", "variant8"} {
		if !IsVariantKind(kind) {
			t.Errorf("IsVariantKind(%q) = false, want true", kind)
		}
	}
	// The cap deliberately matches the op-plan variant cap in llm-contract.md, so
	// the boundaries on both sides matter.
	for _, kind := range []string{
		"variant0",  // below the range
		"variant9",  // one past the cap
		"variant10", // two digits: length check must reject, not truncate to "variant1"
		"variant",   // the bare prefix
		"variant ",  // trailing space
		"Variant1",  // case-sensitive
		"variantA",  // non-digit
		"avariant1", // prefix not anchored at the start
		"variant1x",
		"",
	} {
		if IsVariantKind(kind) {
			t.Errorf("IsVariantKind(%q) = true, want false", kind)
		}
	}
}

func TestIsFileKindAllowlist(t *testing.T) {
	for _, kind := range []string{KindOriginal, KindResult, KindVideo, KindChat, "variant1", "variant8"} {
		if !IsFileKind(kind) {
			t.Errorf("IsFileKind(%q) = false, want true", kind)
		}
	}
	for _, kind := range []string{"", "layout", "thumbnail", "ORIGINAL", "variant9", "../original"} {
		if IsFileKind(kind) {
			t.Errorf("IsFileKind(%q) = true, want false", kind)
		}
	}
}

// The per-file DELETE route removes only filestore-only kinds; original/result go
// with the project. Two invariants matter more than the individual answers.
func TestIsFilestoreOnlyKindIsASubsetThatExcludesOriginalAndResult(t *testing.T) {
	for _, kind := range []string{KindVideo, KindChat, "variant1", "variant8"} {
		if !IsFilestoreOnlyKind(kind) {
			t.Errorf("IsFilestoreOnlyKind(%q) = false, want true", kind)
		}
	}
	if IsFilestoreOnlyKind(KindOriginal) || IsFilestoreOnlyKind(KindResult) {
		t.Error("original/result must not be filestore-only: they are removed with the project")
	}
	// Anything deletable must first be an accepted kind, or the DELETE route would
	// accept a path the filestore rejects.
	for _, kind := range []string{KindOriginal, KindResult, KindVideo, KindChat,
		"variant1", "variant8", "variant9", "", "layout"} {
		if IsFilestoreOnlyKind(kind) && !IsFileKind(kind) {
			t.Errorf("%q is filestore-only but not an accepted file kind", kind)
		}
	}
}

// The kind constants are wire values echoed in REST paths; renaming the Go
// identifier is fine, changing the string is not.
func TestKindConstantWireValues(t *testing.T) {
	got := map[string]string{
		"KindOriginal": KindOriginal,
		"KindResult":   KindResult,
		"KindVideo":    KindVideo,
		"KindChat":     KindChat,
	}
	want := map[string]string{
		"KindOriginal": "original",
		"KindResult":   "result",
		"KindVideo":    "video",
		"KindChat":     "chat",
	}
	if !reflect.DeepEqual(got, want) {
		t.Errorf("file-kind wire values drifted:\n got %v\nwant %v", got, want)
	}
}
