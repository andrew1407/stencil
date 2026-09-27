package store_test

// DB-gated, end to end: STORAGE_QUOTA_PER_SESSION_BYTES through the REST API over the real store and
// filestore — parallel uploads by one session, and a sweep freeing a session's writes for nobody.

import (
	"bytes"
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"sync"
	"testing"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/filestore"
	"stencil/server/internal/httpapi"
	"stencil/server/internal/protocol"
	"stencil/server/internal/store"
)

func TestSessionQuotaHoldsUnderParallelRESTUploads(t *testing.T) {
	s := store.RequireTestStore(t)
	ctx := context.Background()
	fs, err := filestore.New(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	const admin = "quota-admin"
	api := httpapi.New(httpapi.Deps{Projects: s, Sessions: s, Files: fs, Charges: s, SessionQuotaBytes: 100,
		Bus: eventbus.NewInProc(), AdminToken: admin})
	call := func(method, path, tok string, body []byte) *httptest.ResponseRecorder {
		req := httptest.NewRequest(method, path, bytes.NewReader(body))
		req.Header.Set("Authorization", "Bearer "+tok)
		rec := httptest.NewRecorder()
		api.Handler().ServeHTTP(rec, req)
		return rec
	}
	var tok protocol.TokenResponse
	if err := json.Unmarshal(call(http.MethodPost, "/auth/token", admin, nil).Body.Bytes(), &tok); err != nil {
		t.Fatal(err)
	}
	var ids []string
	for i := 0; i < 8; i++ {
		var p protocol.ProjectRecord
		json.Unmarshal(call(http.MethodPost, "/projects", tok.Token, []byte(`{"name":"P","hasImage":true}`)).Body.Bytes(), &p)
		ids = append(ids, p.ID)
	}
	var (
		wg    sync.WaitGroup
		mu    sync.Mutex
		codes = map[int]int{}
	)
	for _, id := range ids {
		wg.Add(1)
		go func(id string) {
			defer wg.Done()
			code := call(http.MethodPost, "/projects/"+id+"/files/original?ext=png", tok.Token, make([]byte, 30)).Code
			mu.Lock()
			codes[code]++
			mu.Unlock()
		}(id)
	}
	wg.Wait()
	if codes[http.StatusCreated] != 3 || codes[http.StatusInsufficientStorage] != 5 {
		t.Fatalf("answers %v, want exactly 3 uploads of 30 B under a 100 B session cap and 5 refused", codes)
	}
	var sess protocol.SessionResponse
	json.Unmarshal(call(http.MethodGet, "/auth/session", tok.Token, nil).Body.Bytes(), &sess)
	if held, err := s.SessionCharged(ctx, sess.SessionID); err != nil || held != 90 {
		t.Fatalf("the session holds %d (%v), want 90", held, err)
	}
	if _, err := s.MigratePool().Exec(ctx, `UPDATE sessions SET expires_at = 1 WHERE id = $1`, sess.SessionID); err != nil {
		t.Fatal(err)
	}
	if _, err := s.DeleteExpiredSessions(ctx, 2, 0); err != nil {
		t.Fatal(err)
	}
	if held, _ := s.SessionCharged(ctx, sess.SessionID); held != 0 {
		t.Fatalf("a swept session still holds %d", held)
	}
}
