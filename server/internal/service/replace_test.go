package service

// Replacing a recorded file on the real filestore: a refused record leaves the old bytes and the old row
// exactly as they were, whatever the new extension; an accepted one swaps both.

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"io"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"testing"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/filestore"
	"stencil/server/internal/protocol"
	"stencil/server/internal/testutil"
)

// replaceID is id-shaped, as the real filestore requires.
const replaceID = "p_abc123_def456"

// replaceRig stores an "old" png original through a working record, returning the row it wrote.
func replaceRig(t *testing.T) (*filestore.Store, *testutil.MemStore, protocol.ProjectRecord) {
	t.Helper()
	files, err := filestore.New(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	st := testutil.NewMemStore()
	st.Seed(protocol.ProjectRecord{ID: replaceID})
	old := FilePut{ID: replaceID, Kind: protocol.KindOriginal, Ext: "png", W: 4, H: 2}
	if _, err := NewFiles(st, files, eventbus.NewInProc()).Store(context.Background(), old, strings.NewReader("old")); err != nil {
		t.Fatal(err)
	}
	rec, _ := st.Project(replaceID)
	return files, st, rec
}

func held(t *testing.T, files *filestore.Store, rec protocol.ProjectRecord) (string, []os.DirEntry) {
	t.Helper()
	f, err := files.OpenByRelPath(rec.OriginalPath)
	if err != nil {
		t.Fatalf("the row's original %s: %v", rec.OriginalPath, err)
	}
	defer f.Close()
	body, _ := io.ReadAll(f)
	entries, _ := os.ReadDir(filepath.Join(files.Root(), filepath.Dir(rec.OriginalPath)))
	return string(body), entries
}

func TestARefusedReplaceLeavesTheOldImageAndRow(t *testing.T) {
	for _, ext := range []string{"png", "jpg"} {
		files, st, before := replaceRig(t)
		put := FilePut{ID: replaceID, Kind: protocol.KindOriginal, Ext: ext, W: 8, H: 8}
		_, err := NewFiles(brokenRecord{st}, files, eventbus.NewInProc()).Store(context.Background(), put, strings.NewReader("new"))
		if !errors.Is(err, ErrRecordFile) {
			t.Fatalf("%s: store = %v, want ErrRecordFile", ext, err)
		}
		after, _ := st.Project(replaceID)
		if !reflect.DeepEqual(before, after) {
			t.Fatalf("%s: the row moved from %+v to %+v", ext, before, after)
		}
		if body, entries := held(t, files, after); body != "old" || len(entries) != 1 {
			t.Fatalf("%s: the row's original reads %q among %d files, want only the old bytes", ext, body, len(entries))
		}
	}
}

func TestAnAcceptedReplaceSwapsTheImageAndRow(t *testing.T) {
	files, st, _ := replaceRig(t)
	put := FilePut{ID: replaceID, Kind: protocol.KindOriginal, Ext: "jpg", W: 8, H: 6}
	if _, err := NewFiles(st, files, eventbus.NewInProc()).Store(context.Background(), put, strings.NewReader("new")); err != nil {
		t.Fatal(err)
	}
	after, _ := st.Project(replaceID)
	sum := sha256.Sum256([]byte("new"))
	if !strings.HasSuffix(after.OriginalPath, ".jpg") || after.ImageW != 8 || after.OriginalHash != hex.EncodeToString(sum[:]) {
		t.Fatalf("row %+v, want the jpg's path, size and hash", after)
	}
	if body, entries := held(t, files, after); body != "new" || len(entries) != 1 {
		t.Fatalf("the row's original reads %q among %d files, want only the new bytes", body, len(entries))
	}
}
