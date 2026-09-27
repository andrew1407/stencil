package httpapi

// STORAGE_QUOTA_PER_OWNER_BYTES over REST: the session that created a project owns its bytes, and an
// upload past that owner's cap is refused exactly as one past the aggregate cap is.

import (
	"encoding/json"
	"net/http"
	"testing"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/filestore"
	"stencil/server/internal/protocol"
	"stencil/server/internal/testutil"
)

func createOwned(t *testing.T, api *API, tok string) string {
	t.Helper()
	rec := do(t, api, http.MethodPost, "/projects", tok, []byte(`{"name":"Q","source":"s","hasImage":true}`))
	if rec.Code != http.StatusCreated {
		t.Fatalf("create: code %d", rec.Code)
	}
	var created protocol.ProjectRecord
	if err := json.Unmarshal(rec.Body.Bytes(), &created); err != nil {
		t.Fatal(err)
	}
	return created.ID
}

func TestUploadAgainstOwnerQuota(t *testing.T) {
	fs, err := filestore.NewWithQuotas(t.TempDir(), filestore.Quotas{PerOwner: 1024})
	if err != nil {
		t.Fatal(err)
	}
	st := testutil.NewMemStore()
	api := New(Deps{Projects: st, Sessions: st, Files: fs, Bus: eventbus.NewInProc(), AdminToken: testAdmin})
	alice, bob := issueToken(t, api, ""), issueToken(t, api, "")
	first, second, bobs := createOwned(t, api, alice), createOwned(t, api, alice), createOwned(t, api, bob)

	chunk := make([]byte, 600)
	upload := func(id, tok string) int {
		return do(t, api, http.MethodPost, "/projects/"+id+"/files/original?ext=png&w=1&h=1", tok, chunk).Code
	}
	if code := upload(first, alice); code != http.StatusCreated {
		t.Fatalf("under the owner's quota: code %d", code)
	}
	// 600 + 600 > 1024 across alice's two projects — whoever uploads, the bytes are the owner's.
	rec := do(t, api, http.MethodPost, "/projects/"+second+"/files/original?ext=png&w=1&h=1", bob, chunk)
	if rec.Code != http.StatusInsufficientStorage {
		t.Fatalf("past the owner's quota should 507, got %d body %s", rec.Code, rec.Body.String())
	}
	var body protocol.ErrorResponse
	if err := json.Unmarshal(rec.Body.Bytes(), &body); err != nil {
		t.Fatal(err)
	}
	if body.Code != protocol.CodeInternal || body.Message != msgQuotaExceeded {
		t.Fatalf("owner-quota answer %+v, want the aggregate quota's shape", body)
	}
	if code := upload(bobs, bob); code != http.StatusCreated {
		t.Fatalf("another owner's upload: code %d", code)
	}
}

// Off by default: the same uploads all fit, as they did before the per-owner quota existed.
func TestOwnerQuotaOffByDefault(t *testing.T) {
	fs, err := filestore.NewWithQuotas(t.TempDir(), filestore.Quotas{})
	if err != nil {
		t.Fatal(err)
	}
	st := testutil.NewMemStore()
	api := New(Deps{Projects: st, Sessions: st, Files: fs, Bus: eventbus.NewInProc(), AdminToken: testAdmin})
	tok := issueToken(t, api, "")
	for i := 0; i < 3; i++ {
		id := createOwned(t, api, tok)
		rec := do(t, api, http.MethodPost, "/projects/"+id+"/files/original?ext=png&w=1&h=1", tok, make([]byte, 600))
		if rec.Code != http.StatusCreated {
			t.Fatalf("upload %d: code %d", i, rec.Code)
		}
	}
}
