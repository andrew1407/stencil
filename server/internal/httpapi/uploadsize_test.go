package httpapi

// The numbers a client states that the store keeps as int4: an upload's ?w= and ?h=, and an update's
// version. An impossible one is a 400 before any byte moves, never a store error after.

import (
	"encoding/json"
	"net/http"
	"testing"

	"stencil/server/internal/protocol"
)

func TestUploadRefusesAnImpossibleSizeBeforeTheBytesMove(t *testing.T) {
	api, _ := testAPI(t, "")
	tok := issueToken(t, api, "")
	rec := do(t, api, http.MethodPost, "/projects", tok, []byte(`{"name":"Sized","hasImage":true}`))
	var p protocol.ProjectRecord
	json.Unmarshal(rec.Body.Bytes(), &p)
	base := "/projects/" + p.ID + "/files/original?ext=png"
	if r := do(t, api, http.MethodPost, base+"&w=4&h=3", tok, []byte("first")); r.Code != http.StatusCreated {
		t.Fatalf("a valid upload: %d %s", r.Code, r.Body.String())
	}
	for query, want := range map[string]string{
		"&w=3000000000&h=3": "w must be an integer 0..65535",
		"&w=-1&h=3":         "w must be an integer 0..65535",
		"&w=4&h=abc":        "h must be an integer 0..65535",
		"&w=4&h=65536":      "h must be an integer 0..65535",
	} {
		r := do(t, api, http.MethodPost, base+query, tok, []byte("second"))
		var e protocol.ErrorResponse
		json.Unmarshal(r.Body.Bytes(), &e)
		if r.Code != http.StatusBadRequest || e.Message != want {
			t.Errorf("%s: %d %s, want 400 %q", query, r.Code, r.Body.String(), want)
		}
	}
	if r := do(t, api, http.MethodGet, "/projects/"+p.ID+"/files/original", tok, nil); r.Body.String() != "first" {
		t.Fatalf("a refused upload moved the stored original: %q", r.Body.String())
	}
	if r := do(t, api, http.MethodPost, base, tok, []byte("third")); r.Code != http.StatusCreated {
		t.Fatalf("an upload naming no size is still taken: %d", r.Code)
	}
}

func TestUpdateRefusesAVersionNoRowCanHold(t *testing.T) {
	api, _ := testAPI(t, "")
	tok := issueToken(t, api, "")
	rec := do(t, api, http.MethodPost, "/projects", tok, []byte(`{"name":"V","hasImage":true}`))
	var p protocol.ProjectRecord
	json.Unmarshal(rec.Body.Bytes(), &p)
	for _, v := range []string{"2147483648", "-1"} {
		r := do(t, api, http.MethodPut, "/projects/"+p.ID, tok, []byte(`{"name":"W","version":`+v+`}`))
		var e protocol.ErrorResponse
		json.Unmarshal(r.Body.Bytes(), &e)
		if r.Code != http.StatusBadRequest || e.Message != "version must be 0..2147483647" {
			t.Errorf("version %s: %d %s, want 400", v, r.Code, r.Body.String())
		}
	}
}
