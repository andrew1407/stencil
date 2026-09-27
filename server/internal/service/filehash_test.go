package service

// The original's hash: taken from the bytes as they stream to disk, recorded on the row, replaced by a
// new original and left alone by a result upload.

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"strings"
	"testing"

	"stencil/server/internal/protocol"
)

func sha(s string) string {
	sum := sha256.Sum256([]byte(s))
	return hex.EncodeToString(sum[:])
}

func TestStoreRecordsTheOriginalsHash(t *testing.T) {
	svc, st, files := fileSvc(t)
	st.Seed(protocol.ProjectRecord{ID: "p_1"})
	if _, err := svc.Store(context.Background(), put(protocol.KindOriginal), strings.NewReader("first")); err != nil {
		t.Fatal(err)
	}
	rec, _ := st.Project("p_1")
	if rec.OriginalHash != sha("first") {
		t.Fatalf("hash = %q, want %q", rec.OriginalHash, sha("first"))
	}
	if got := files.stored["p_1/original"]; got != "first" {
		t.Fatalf("the hashing tee changed the stored bytes: %q", got)
	}
	// Same size, same extension, other bytes: only the hash can tell the pictures apart.
	resp, err := svc.Store(context.Background(), put(protocol.KindOriginal), strings.NewReader("other"))
	if err != nil {
		t.Fatal(err)
	}
	if rec, _ = st.Project("p_1"); rec.OriginalHash != sha("other") || resp.OriginalHash != rec.OriginalHash {
		t.Fatalf("a replaced original: record hash %q, response hash %q", rec.OriginalHash, resp.OriginalHash)
	}
	if resp, err = svc.Store(context.Background(), put(protocol.KindResult), strings.NewReader("render")); err != nil {
		t.Fatal(err)
	}
	if rec, _ = st.Project("p_1"); rec.OriginalHash != sha("other") || resp.OriginalHash != "" {
		t.Fatalf("a result upload: record hash %q, response hash %q", rec.OriginalHash, resp.OriginalHash)
	}
}

// A failed write records nothing, so no hash can name bytes that never landed.
func TestStoreRecordsNoHashForAFailedWrite(t *testing.T) {
	svc, st, files := fileSvc(t)
	st.Seed(protocol.ProjectRecord{ID: "p_1", OriginalHash: sha("kept")})
	files.putErr = context.Canceled
	if _, err := svc.Store(context.Background(), put(protocol.KindOriginal), strings.NewReader("lost")); err == nil {
		t.Fatal("a failed write reported success")
	}
	if rec, _ := st.Project("p_1"); rec.OriginalHash != sha("kept") {
		t.Fatalf("hash = %q after a failed write", rec.OriginalHash)
	}
}
