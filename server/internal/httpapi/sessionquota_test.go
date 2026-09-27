package httpapi

// STORAGE_QUOTA_PER_SESSION_BYTES over REST: the session that sends the bytes is charged, whoever owns
// the project, and is refused exactly as the other quotas refuse; a create's inline original is one such
// write. Off, the ledger is never touched.

import (
	"encoding/base64"
	"encoding/json"
	"net/http"
	"strings"
	"testing"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/filestore"
	"stencil/server/internal/protocol"
	"stencil/server/internal/testutil"
)

func sessionQuotaAPI(t *testing.T, quota int64) (*API, *testutil.MemStore) {
	t.Helper()
	fs, err := filestore.NewWithQuotas(t.TempDir(), filestore.Quotas{})
	if err != nil {
		t.Fatal(err)
	}
	st := testutil.NewMemStore()
	return New(Deps{Projects: st, Sessions: st, Files: fs, Charges: st, SessionQuotaBytes: quota,
		Bus: eventbus.NewInProc(), AdminToken: testAdmin}), st
}

func wantQuotaAnswer(t *testing.T, code int, body []byte) {
	t.Helper()
	var got protocol.ErrorResponse
	if err := json.Unmarshal(body, &got); err != nil || code != http.StatusInsufficientStorage ||
		got.Code != protocol.CodeInternal || got.Message != msgQuotaExceeded {
		t.Fatalf("past the session quota: %d %s, want the other quotas' 507", code, body)
	}
}

func TestUploadAgainstSessionQuota(t *testing.T) {
	api, _ := sessionQuotaAPI(t, 1024)
	alice, bob := issueToken(t, api, ""), issueToken(t, api, "")
	mine, bobs := createOwned(t, api, alice), createOwned(t, api, bob)
	up := func(id, kind, tok string, n int) (int, []byte) {
		rec := do(t, api, http.MethodPost, "/projects/"+id+"/files/"+kind+"?ext=png&w=1&h=1", tok, make([]byte, n))
		return rec.Code, rec.Body.Bytes()
	}
	if code, _ := up(mine, "original", alice, 600); code != http.StatusCreated {
		t.Fatalf("under the cap: %d", code)
	}
	// Bob's project, alice's bytes: the writer is capped, not the owner.
	code, body := up(bobs, "original", alice, 600)
	wantQuotaAnswer(t, code, body)
	if code, _ := up(mine, "result", bob, 600); code != http.StatusCreated {
		t.Fatalf("bob writing into alice's project: %d", code)
	}
	// Bob replaces alice's original: the charge is his now, and alice has room again.
	if code, _ := up(mine, "original", bob, 300); code != http.StatusCreated {
		t.Fatalf("bob's replacement: %d", code)
	}
	if code, _ := up(bobs, "variant1", alice, 1000); code != http.StatusCreated {
		t.Fatalf("alice after her charge moved: %d", code)
	}
	if rec := do(t, api, http.MethodDelete, "/projects/"+bobs+"/files/variant1", alice, nil); rec.Code != http.StatusNoContent {
		t.Fatalf("delete: %d", rec.Code)
	}
	if code, _ := up(bobs, "chat", alice, 1024); code != http.StatusCreated {
		t.Fatalf("a delete credits its writer: %d", code)
	}
}

// A create's inline original is charged to the creator; refused, no project is left behind.
func TestCreateInlineOriginalAgainstSessionQuota(t *testing.T) {
	api, _ := sessionQuotaAPI(t, 8)
	tok := issueToken(t, api, "")
	create := func(payload []byte) (int, []byte) {
		body := `{"name":"Q","hasImage":true,"originalContent":"data:image/png;base64,` + base64.StdEncoding.EncodeToString(payload) + `"}`
		rec := do(t, api, http.MethodPost, "/projects", tok, []byte(body))
		return rec.Code, rec.Body.Bytes()
	}
	if code, body := create([]byte("12345")); code != http.StatusCreated || !strings.Contains(string(body), `"originalPath"`) {
		t.Fatalf("under the cap: %d %s", code, body)
	}
	code, body := create([]byte("12345"))
	wantQuotaAnswer(t, code, body)
	if list := listProjects(t, api, tok, ""); len(list.Projects) != 1 {
		t.Fatalf("a refused create left %d projects, want 1", len(list.Projects))
	}
	rec := do(t, api, http.MethodPost, "/projects", tok, []byte(`{"name":"Q","hasImage":true,"originalContent":"https://e/i.png"}`))
	var bad protocol.ErrorResponse
	if json.Unmarshal(rec.Body.Bytes(), &bad); rec.Code != http.StatusBadRequest || bad.Message != msgBadOriginal {
		t.Fatalf("a non-image inline original: %d %s", rec.Code, rec.Body.String())
	}
	if len(listProjects(t, api, tok, "").Projects) != 1 {
		t.Fatal("a refused inline original created a project")
	}
}

// Off by default: uploads, deletes and inline originals never reach the ledger.
func TestSessionQuotaOffTouchesNoLedger(t *testing.T) {
	api, st := sessionQuotaAPI(t, 0)
	tok := issueToken(t, api, "")
	id := createOwned(t, api, tok)
	for _, kind := range []string{"original", "result", "variant3"} {
		if rec := do(t, api, http.MethodPost, "/projects/"+id+"/files/"+kind+"?ext=png", tok, make([]byte, 4096)); rec.Code != http.StatusCreated {
			t.Fatalf("%s: %d", kind, rec.Code)
		}
	}
	do(t, api, http.MethodDelete, "/projects/"+id+"/files/variant3", tok, nil)
	do(t, api, http.MethodPost, "/projects", tok, []byte(`{"hasImage":true,"originalContent":"data:image/png;base64,AAAA"}`))
	if n := st.LedgerCalls(); n != 0 {
		t.Fatalf("the ledger was called %d times with the per-session quota off", n)
	}
}
