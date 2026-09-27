package hub

import (
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"

	"stencil/server/internal/auth"
	"stencil/server/internal/eventbus"
	"stencil/server/internal/filestore"
	"stencil/server/internal/httpapi"
	"stencil/server/internal/protocol"
	"stencil/server/internal/testutil"
)

// liveREST is a hub and the REST API over one store and one bus, wired as the server wires them.
func liveREST(t *testing.T, seed protocol.ProjectRecord) (string, http.Handler) {
	t.Helper()
	st := testutil.NewMemStore()
	st.Seed(seed)
	if _, err := st.CreateSession(context.Background(), auth.HashToken(goodToken), "t", 0, 0); err != nil {
		t.Fatal(err)
	}
	bus := eventbus.NewInProc()
	h := New(context.Background(), st, bus, st)
	t.Cleanup(h.Close)
	fs, err := filestore.New(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	api := httpapi.New(httpapi.Deps{Projects: st, Sessions: st, Files: fs, Bus: bus, LiveSessions: h, AdminToken: "admin"})
	return startTCP(t, h), api.Handler()
}

func rest(t *testing.T, api http.Handler, method, path string, body []byte) *httptest.ResponseRecorder {
	t.Helper()
	req := httptest.NewRequest(method, path, bytes.NewReader(body))
	req.Header.Set("Authorization", "Bearer "+goodToken)
	rec := httptest.NewRecorder()
	api.ServeHTTP(rec, req)
	if rec.Code >= 300 {
		t.Fatalf("%s %s: %d %s", method, path, rec.Code, rec.Body.String())
	}
	return rec
}

// welcomeOnceFresh joins a new client until its welcome passes fresh: the write reaches the session over
// the bus, so a joiner racing it may still see the snapshot before.
func welcomeOnceFresh(t *testing.T, addr string, fresh func(protocol.WSMessage) bool) protocol.WSMessage {
	t.Helper()
	deadline := time.Now().Add(3 * time.Second)
	for i := 0; ; i++ {
		c, err := testutil.DialTCP(addr)
		if err != nil {
			t.Fatal(err)
		}
		send(t, c, protocol.WSMessage{Type: protocol.WSHello, Token: goodToken, ProjectID: "p_t_a", ClientID: fmt.Sprint("late", i)})
		send(t, c, protocol.WSMessage{Type: protocol.WSSubscribe})
		w := readUntil(t, c, protocol.WSWelcome)
		c.Close(0, "")
		if fresh(w) || time.Now().After(deadline) {
			return w
		}
		time.Sleep(10 * time.Millisecond)
	}
}

// A REST upload during a live session: a later joiner's welcome names the new original and version.
func TestWelcomeAfterARestUploadNamesTheNewOriginal(t *testing.T) {
	addr, api := liveREST(t, protocol.ProjectRecord{ID: "p_t_a", HasImage: true, OriginalHash: "old"})
	joinProject(t, addr, "p_t_a", "A") // keeps the session, and its cached snapshot, alive
	img := []byte("\x89PNG replaced")
	rest(t, api, http.MethodPost, "/projects/p_t_a/files/original?ext=png&w=4&h=3", img)
	sum := sha256.Sum256(img)
	want := hex.EncodeToString(sum[:])

	w := welcomeOnceFresh(t, addr, func(w protocol.WSMessage) bool { return w.Version == 1 })
	if w.Version != 1 || w.Project == nil || w.Project.Version != 1 || w.Project.OriginalHash != want {
		t.Fatalf("welcome after the upload: version %d, project %+v; want version 1 and hash %s", w.Version, w.Project, want)
	}
}

// A REST layout update during a live session: a later joiner starts from it, not from the cached one.
func TestWelcomeAfterARestUpdateCarriesItsLayout(t *testing.T) {
	addr, api := liveREST(t, protocol.ProjectRecord{ID: "p_t_a", Layout: json.RawMessage(`{"lines":[1]}`)})
	joinProject(t, addr, "p_t_a", "A")
	rest(t, api, http.MethodPut, "/projects/p_t_a", []byte(`{"layout":{"lines":[2]},"version":0}`))

	w := welcomeOnceFresh(t, addr, func(w protocol.WSMessage) bool { return w.Version == 1 })
	if w.Version != 1 || string(w.Layout) != `{"lines":[2]}` {
		t.Fatalf("welcome after the update: version %d layout %s; want 1 and the new layout", w.Version, w.Layout)
	}
}

// The run-loop's half, driven directly: a write reported while a load runs is read again, the waiting
// welcome takes the second read, and the session's own save echo reads nothing.
func TestAWriteReportedMidLoadIsReadAgain(t *testing.T) {
	done := make(chan struct{})
	defer close(done)
	s := &session{id: "p_t_a", members: map[string]*member{},
		hub:     &Hub{ctx: context.Background(), bus: eventbus.NewInProc()},
		persist: &snapshotWorker{jobs: make(chan persistJob, 4), done: done}}
	m := newMember("B", "B", nil, defaultTuning)
	s.members["B"] = m
	loads := func() int { return len(s.persist.jobs) }

	s.sendWelcome(m) // the first load goes out; the welcome waits for it
	s.refresh(2)     // a write lands while it runs
	s.applyResult(persistResult{kind: persistLoad, rec: protocol.ProjectRecord{ID: "p_t_a", Version: 1}})
	if loads() != 2 || len(m.out) != 0 {
		t.Fatalf("after a read that predates the write: %d loads, %d frames; want 2 loads, no welcome yet", loads(), len(m.out))
	}
	s.applyResult(persistResult{kind: persistLoad, rec: protocol.ProjectRecord{ID: "p_t_a", Version: 2, OriginalHash: "new"}})
	var w protocol.WSMessage
	if len(m.out) != 1 || json.Unmarshal(<-m.out, &w) != nil || w.Version != 2 || w.Project.OriginalHash != "new" {
		t.Fatalf("the waiting welcome = %+v, want the second read at version 2", w)
	}
	s.refresh(2) // the echo of what the session already holds
	if loads() != 2 || s.stale {
		t.Fatalf("an echo started a load (%d) or left the snapshot stale (%v)", loads(), s.stale)
	}
}
