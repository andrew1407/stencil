package service

// The legacy inline original: only a base64 image data URL is taken, and a create stores it through the
// upload path or not at all; the 0006 mover shares the parser and hashes as an upload does.

import (
	"context"
	"errors"
	"strings"
	"testing"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/filestore"
	"stencil/server/internal/protocol"
	"stencil/server/internal/store"
	"stencil/server/internal/testutil"
)

func TestParseInlineOriginal(t *testing.T) {
	for in, ext := range map[string]string{
		"data:image/png;base64,AAAA":                   "png",
		"DATA:Image/JPEG;name=a.jpg;BASE64,AA\nAA":     "jpeg",
		"data:image/webp;base64,":                      "webp", // empty: the upload path refuses it
		"data:image/svg+xml;charset=utf-8;base64,AAA=": "svg+xml",
	} {
		if o, err := parseInlineOriginal(in); err != nil || o.ext != ext {
			t.Errorf("%q: %v %q, want ext %q", in, err, o.ext, ext)
		}
	}
	for _, bad := range []string{
		"", "AAAA", "https://e/i.png", "data:image/png,AAAA", "data:text/plain;base64,AAAA",
		"data:;base64,AAAA", "data:image/;base64,AAAA", "data:image/png;base64,A*A=", "data:image/png;base64",
	} {
		if _, err := parseInlineOriginal(bad); !errors.Is(err, ErrBadOriginal) {
			t.Errorf("%q: %v, want ErrBadOriginal", bad, err)
		}
	}
}

// originalRig is a project service whose inline originals land through a real file service.
func originalRig(t *testing.T) (*ProjectService, *testutil.MemStore, *fakeFiles, <-chan eventbus.Envelope) {
	t.Helper()
	svc, st, files := projectSvc(t, 0)
	svc.Originals = NewFiles(st, files, svc.Bus)
	events, unsub := svc.Bus.Subscribe(eventbus.ChannelEvents)
	t.Cleanup(unsub)
	return svc, st, files, events
}

func TestCreateStoresAnInlineOriginalAsTheFile(t *testing.T) {
	svc, _, files, events := originalRig(t)
	req := protocol.CreateProjectRequest{Name: "n", HasImage: true, ImageW: 4, ImageH: 3,
		OriginalContent: "data:image/png;base64,Zmlyc3Q="}
	rec, err := svc.Create(context.Background(), "s_1", req)
	if err != nil {
		t.Fatal(err)
	}
	if rec.OriginalPath == "" || rec.OriginalHash != sha("first") || rec.ImageW != 4 || rec.Version != 1 {
		t.Fatalf("created record %+v, want the stored original's path and hash", rec)
	}
	if got := files.stored[rec.ID+"/original"]; got != "first" {
		t.Fatalf("stored %q, want the decoded bytes", got)
	}
	if env := <-events; env.Type != protocol.WSProjectEv || !strings.Contains(string(env.Data), `"created"`) {
		t.Fatalf("first event %s, want the one created announcement", env.Data)
	}
	select {
	case env := <-events:
		t.Fatalf("a create announced twice: %s", env.Data)
	default:
	}
}

// A refused original takes the new row back, and nothing is announced.
func TestCreateTakesTheRowBackWhenItsOriginalIsRefused(t *testing.T) {
	svc, st, files, events := originalRig(t)
	files.putErr = filestore.ErrQuotaExceeded
	req := protocol.CreateProjectRequest{Name: "n", HasImage: true, OriginalContent: "data:image/png;base64,AAAA"}
	_, err := svc.Create(context.Background(), "s_1", req)
	if !errors.Is(err, ErrStoreOriginal) || !errors.Is(err, filestore.ErrQuotaExceeded) {
		t.Fatalf("err = %v, want ErrStoreOriginal wrapping the quota refusal", err)
	}
	if list, _ := st.ListProjects(context.Background(), listAll); len(list) != 0 {
		t.Fatalf("a refused create left %d rows", len(list))
	}
	if len(files.removed) != 1 {
		t.Fatalf("removed = %v, want the new project's bytes dropped", files.removed)
	}
	if _, err := svc.Create(context.Background(), "", protocol.CreateProjectRequest{HasImage: true, OriginalContent: "x"}); !errors.Is(err, ErrBadOriginal) {
		t.Fatalf("a bad inline original: %v", err)
	}
	select {
	case env := <-events:
		t.Fatalf("a refused create was announced: %s", env.Data)
	default:
	}
}

func TestLegacyOriginalsMovesAndRefusesDeterministically(t *testing.T) {
	files := &fakeFiles{}
	move := LegacyOriginals(files)
	f, err := move(context.Background(), store.LegacyOriginal{ID: "p_1", Content: "data:image/gif;base64,Zmlyc3Q="})
	if err != nil || f.Path != "projects/p_1/original.gif" || f.Hash != sha("first") || f.Kind != protocol.KindOriginal {
		t.Fatalf("moved %+v %v", f, err)
	}
	for content, putErr := range map[string]error{
		"not a data url":                 nil,
		"data:image/png;base64,":         filestore.ErrEmpty,
		"data:image/svg+xml;base64,AAA=": filestore.ErrUnsafePath{Reason: "extension svg+xml"},
	} {
		files.putErr = putErr
		if _, err := move(context.Background(), store.LegacyOriginal{ID: "p_2", Content: content}); !errors.Is(err, store.ErrUndecodable) {
			t.Fatalf("%q: %v, want ErrUndecodable", content, err)
		}
	}
	files.putErr = errors.New("disk full")
	if _, err := move(context.Background(), store.LegacyOriginal{ID: "p_3", Content: "data:image/png;base64,AAAA"}); err == nil ||
		errors.Is(err, store.ErrUndecodable) {
		t.Fatalf("a failed write: %v, want it passed through as a failure, not a drop", err)
	}
}
