package httpapi

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"

	"stencil/server/internal/protocol"
)

// GET /projects/{id} carries an ETag naming the version; sending it back answers a bodiless 304 while
// the project is unchanged, and the full record once a write bumped it.
func TestProjectGetAnswers304WhileTheVersionHolds(t *testing.T) {
	api, _ := testAPI(t, "")
	tok := issueToken(t, api, "")
	rec := do(t, api, http.MethodPost, "/projects", tok, []byte(`{"name":"E","hasImage":true,"layout":{"lines":[]}}`))
	var created protocol.ProjectRecord
	_ = json.Unmarshal(rec.Body.Bytes(), &created)

	get := func(inm string) *httptest.ResponseRecorder {
		req := httptest.NewRequest(http.MethodGet, "/projects/"+created.ID, nil)
		req.Header.Set("Authorization", "Bearer "+tok)
		if inm != "" {
			req.Header.Set("If-None-Match", inm)
		}
		out := httptest.NewRecorder()
		api.Handler().ServeHTTP(out, req)
		return out
	}
	first := get("")
	etag := first.Header().Get("ETag")
	if first.Code != http.StatusOK || etag == "" {
		t.Fatalf("first read: %d etag %q", first.Code, etag)
	}
	if again := get(etag); again.Code != http.StatusNotModified || again.Body.Len() != 0 {
		t.Fatalf("unchanged: %d with %d body bytes, want a bodiless 304", again.Code, again.Body.Len())
	}
	name := `{"name":"F","version":` + jsonInt(created.Version) + `}`
	if upd := do(t, api, http.MethodPut, "/projects/"+created.ID, tok, []byte(name)); upd.Code != http.StatusOK {
		t.Fatalf("update: %d %s", upd.Code, upd.Body.String())
	}
	changed := get(etag)
	if changed.Code != http.StatusOK || changed.Header().Get("ETag") == etag {
		t.Fatalf("after a write: %d etag %q, want the new record under a new tag", changed.Code, changed.Header().Get("ETag"))
	}
}

func jsonInt(v int64) string { b, _ := json.Marshal(v); return string(b) }
