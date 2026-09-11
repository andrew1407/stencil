package filestore

import (
	"bytes"
	"errors"
	"io"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"stencil/server/internal/protocol"
)

// leftoverTemps reports temp files the store failed to clean up.
func leftoverTemps(t *testing.T, s *Store, id string) []string {
	t.Helper()
	entries, err := os.ReadDir(filepath.Join(s.Root(), "projects", id))
	if err != nil {
		return nil
	}
	var out []string
	for _, e := range entries {
		if strings.HasPrefix(e.Name(), ".tmp-") {
			out = append(out, e.Name())
		}
	}
	return out
}

// An upload arrives as a stream, not a []byte: the bytes land on disk without the
// whole file passing through the heap first.
func TestPutStreamRoundTrip(t *testing.T) {
	s := newTestStore(t)
	want := []byte("streamed \x00 bytes")
	rel, err := s.PutStream(validID, protocol.KindOriginal, "png", bytes.NewReader(want))
	if err != nil {
		t.Fatalf("PutStream: %v", err)
	}
	if rel != "projects/"+validID+"/original.png" {
		t.Fatalf("unexpected rel path %q", rel)
	}
	got, err := s.Get(validID, protocol.KindOriginal, "png")
	if err != nil || !bytes.Equal(got, want) {
		t.Fatalf("round trip: %v %q", err, got)
	}
	if temps := leftoverTemps(t, s, validID); temps != nil {
		t.Fatalf("temp files left behind: %v", temps)
	}
}

// A body with no bytes is rejected, and nothing is committed for it.
func TestPutStreamRejectsEmpty(t *testing.T) {
	s := newTestStore(t)
	if _, err := s.PutStream(validID, protocol.KindResult, "png", bytes.NewReader(nil)); !errors.Is(err, ErrEmpty) {
		t.Fatalf("empty body: %v", err)
	}
	if _, err := s.Get(validID, protocol.KindResult, "png"); !errors.Is(err, ErrNotFound) {
		t.Fatal("an empty write must commit nothing")
	}
	if temps := leftoverTemps(t, s, validID); temps != nil {
		t.Fatalf("temp files left behind: %v", temps)
	}
}

// errReader fails part way through, standing in for a dropped upload.
type errReader struct{ n int }

func (r *errReader) Read(p []byte) (int, error) {
	if r.n <= 0 {
		return 0, errors.New("connection reset")
	}
	r.n--
	p[0] = 'x'
	return 1, nil
}

// A stream that dies mid-upload leaves the previous bytes in place: the rename
// only happens once the whole body is on disk and fsynced.
func TestPutStreamFailureKeepsPreviousBytes(t *testing.T) {
	s := newTestStore(t)
	if _, err := s.Put(validID, protocol.KindOriginal, "png", []byte("v1")); err != nil {
		t.Fatal(err)
	}
	if _, err := s.PutStream(validID, protocol.KindOriginal, "png", &errReader{n: 3}); err == nil {
		t.Fatal("a broken stream must fail the write")
	}
	got, err := s.Get(validID, protocol.KindOriginal, "png")
	if err != nil || string(got) != "v1" {
		t.Fatalf("the committed bytes must survive: %v %q", err, got)
	}
	if temps := leftoverTemps(t, s, validID); temps != nil {
		t.Fatalf("temp files left behind: %v", temps)
	}
}

// The quota is still enforced on the streaming path, and a rejected write leaves
// nothing behind — the cap covers the committed store.
func TestPutStreamHonoursQuota(t *testing.T) {
	s, err := NewWithQuota(t.TempDir(), 64)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := s.PutStream(validID, protocol.KindOriginal, "png", io.LimitReader(zeroes{}, 40)); err != nil {
		t.Fatalf("first write: %v", err)
	}
	if _, err := s.PutStream(validID, protocol.KindResult, "png", io.LimitReader(zeroes{}, 40)); !errors.Is(err, ErrQuotaExceeded) {
		t.Fatalf("over-quota stream: %v", err)
	}
	if _, err := s.Get(validID, protocol.KindResult, "png"); !errors.Is(err, ErrNotFound) {
		t.Fatal("a quota-rejected write must commit nothing")
	}
	if temps := leftoverTemps(t, s, validID); temps != nil {
		t.Fatalf("temp files left behind: %v", temps)
	}
}

// zeroes is an endless reader of NUL bytes.
type zeroes struct{}

func (zeroes) Read(p []byte) (int, error) { return len(p), nil }
