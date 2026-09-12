package httpapi

// Shared REST test harness: one API over fakes plus a real filestore, a token,
// and a request helper. The route suites live beside this file.

import (
	"bytes"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"

	"stencil/server/internal/bus"
	"stencil/server/internal/filestore"
	"stencil/server/internal/protocol"
	"stencil/server/internal/testutil"
)

// testAdmin is the admin token every test API runs with: issuance is always
// gated now (empty AdminToken = closed, not open), so the helpers default to it.
const testAdmin = "test-admin-token"

// testAPI wires the API with shared fakes plus a real filestore + in-proc bus.
// An empty adminToken means "the shared testAdmin", not open issuance.
func testAPI(t *testing.T, adminToken string) (*API, *testutil.MemStore) {
	t.Helper()
	if adminToken == "" {
		adminToken = testAdmin
	}
	fs, err := filestore.New(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	st := testutil.NewMemStore()
	api := New(Deps{
		Projects: st, Sessions: st, Files: fs, Bus: bus.NewInProc(),
		AdminToken: adminToken,
	})
	return api, st
}

// issueToken mints a token via the API and returns it. An empty admin defaults
// to the shared testAdmin.
func issueToken(t *testing.T, api *API, admin string) string {
	t.Helper()
	if admin == "" {
		admin = testAdmin
	}
	req := httptest.NewRequest(http.MethodPost, "/auth/token", nil)
	req.Header.Set("X-Admin-Token", admin)
	rec := httptest.NewRecorder()
	api.Handler().ServeHTTP(rec, req)
	if rec.Code != http.StatusOK {
		t.Fatalf("issue token: code %d body %s", rec.Code, rec.Body.String())
	}
	var resp protocol.TokenResponse
	json.Unmarshal(rec.Body.Bytes(), &resp)
	return resp.Token
}

func do(t *testing.T, api *API, method, path, token string, body []byte) *httptest.ResponseRecorder {
	t.Helper()
	var req *http.Request
	if body != nil {
		req = httptest.NewRequest(method, path, bytes.NewReader(body))
	} else {
		req = httptest.NewRequest(method, path, nil)
	}
	if token != "" {
		req.Header.Set("Authorization", "Bearer "+token)
	}
	rec := httptest.NewRecorder()
	api.Handler().ServeHTTP(rec, req)
	return rec
}
