package httpapi

import (
	"encoding/json"
	"net/http"
	"testing"
	"time"
)

// Updates and deletes spend the same per-session write bucket as creations and uploads, and the
// refusal carries the configured Retry-After.
func TestUpdatesAndDeletesShareTheWriteBudget(t *testing.T) {
	api := limitAPI(t, Deps{WriteRatePerMin: 3, RetryAfter: 90 * time.Second})
	tok := issueToken(t, api, "")
	rec := do(t, api, http.MethodPost, "/projects", tok, []byte(`{"name":"P","hasImage":true}`))
	var p struct{ ID string }
	if err := json.Unmarshal(rec.Body.Bytes(), &p); err != nil || p.ID == "" {
		t.Fatalf("create: %d %s", rec.Code, rec.Body.String())
	}
	if rec := do(t, api, http.MethodPut, "/projects/"+p.ID, tok, []byte(`{"name":"Q","version":0}`)); rec.Code != http.StatusOK {
		t.Fatalf("update: %d %s", rec.Code, rec.Body.String())
	}
	if rec := do(t, api, http.MethodDelete, "/projects/"+p.ID+"/files/chat", tok, nil); rec.Code != http.StatusNoContent {
		t.Fatalf("file delete: %d", rec.Code)
	}
	for _, r := range []struct{ method, path string }{
		{http.MethodPut, "/projects/" + p.ID}, {http.MethodDelete, "/projects/" + p.ID},
		{http.MethodDelete, "/projects/" + p.ID + "/files/chat"},
	} {
		rec := do(t, api, r.method, r.path, tok, []byte(`{"version":1}`))
		want429(t, rec, r.method+" "+r.path)
		if got := rec.Header().Get("Retry-After"); got != "90" {
			t.Errorf("%s %s: Retry-After %q, want the configured 90", r.method, r.path, got)
		}
	}
}
