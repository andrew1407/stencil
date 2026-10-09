package httpapi

// The per-field caps on project metadata (internal/validate): a create or update past one answers 400
// with the reason, and a body at the cap is stored.

import (
	"encoding/json"
	"net/http"
	"strings"
	"testing"

	"stencil/server/internal/protocol"
	"stencil/server/internal/validate"
)

// overCap pairs a body past one cap with the message it must answer.
var overCap = []struct{ name, body, want string }{
	{"name", `{"name":"` + strings.Repeat("n", validate.MaxNameChars+1) + `","hasImage":true}`, "name must be at most 80 characters"},
	{"description", `{"description":"` + strings.Repeat("d", validate.MaxDescriptionChars+1) + `","hasImage":true}`, "description must be at most 2000 characters"},
	{"source", `{"source":"` + strings.Repeat("s", validate.MaxURLChars+1) + `","hasImage":true}`, "source must be at most 2048 characters"},
	{"resource", `{"resource":"` + strings.Repeat("r", validate.MaxURLChars+1) + `","hasImage":true}`, "resource must be at most 2048 characters"},
	{"color", `{"color":"#ggg","hasImage":true}`, "color must be #rrggbb"},
	{"blankColor", `{"blankColor":"ffffff","hasImage":true}`, "blankColor must be #rrggbb"},
	{"keyword count", `{"keywords":[` + strings.Repeat(`"k",`, validate.MaxKeywords) + `"k"],"hasImage":true}`, "keywords must be at most 32 entries"},
	{"keyword length", `{"keywords":["` + strings.Repeat("k", validate.MaxKeywordChars+1) + `"],"hasImage":true}`, "keyword must be at most 64 characters"},
}

func TestCreateRefusesEachFieldPastItsCap(t *testing.T) {
	api, _ := testAPI(t, "")
	tok := issueToken(t, api, "")
	for _, tc := range overCap {
		rec := do(t, api, http.MethodPost, "/projects", tok, []byte(tc.body))
		var e protocol.ErrorResponse
		json.Unmarshal(rec.Body.Bytes(), &e)
		if rec.Code != http.StatusBadRequest || e.Code != protocol.CodeBadRequest || e.Message != tc.want {
			t.Errorf("%s: %d %s, want 400 %q", tc.name, rec.Code, rec.Body.String(), tc.want)
		}
	}
	lr := do(t, api, http.MethodGet, "/projects", tok, nil)
	var list protocol.ProjectListResponse
	json.Unmarshal(lr.Body.Bytes(), &list)
	if len(list.Projects) != 0 {
		t.Fatalf("a refused create left a project: %+v", list.Projects)
	}
}

func TestUpdateRefusesEachFieldPastItsCap(t *testing.T) {
	api, _ := testAPI(t, "")
	tok := issueToken(t, api, "")
	rec := do(t, api, http.MethodPost, "/projects", tok, []byte(`{"name":"Capped","hasImage":true}`))
	var p protocol.ProjectRecord
	json.Unmarshal(rec.Body.Bytes(), &p)
	for _, tc := range overCap {
		if tc.name == "source" || tc.name == "resource" {
			continue // an update cannot name either
		}
		body := strings.Replace(tc.body, `,"hasImage":true}`, `,"version":0}`, 1)
		r := do(t, api, http.MethodPut, "/projects/"+p.ID, tok, []byte(body))
		var e protocol.ErrorResponse
		json.Unmarshal(r.Body.Bytes(), &e)
		if r.Code != http.StatusBadRequest || e.Message != tc.want {
			t.Errorf("%s: %d %s, want 400 %q", tc.name, r.Code, r.Body.String(), tc.want)
		}
	}
	if r := do(t, api, http.MethodGet, "/projects/"+p.ID, tok, nil); !strings.Contains(r.Body.String(), `"version":0`) {
		t.Fatalf("a refused update moved the version: %s", r.Body.String())
	}
}

// A body exactly at every cap is stored; characters, not bytes, are what the caps count.
func TestFieldsAtTheCapAreStored(t *testing.T) {
	api, _ := testAPI(t, "")
	tok := issueToken(t, api, "")
	name := strings.Repeat("ñ", validate.MaxNameChars)
	body := `{"name":"` + name + `","description":"` + strings.Repeat("d", validate.MaxDescriptionChars) +
		`","source":"` + strings.Repeat("s", validate.MaxURLChars) + `","color":"#AbCdEf","blankColor":"","keywords":[` +
		strings.Repeat(`"k",`, validate.MaxKeywords-1) + `"` + strings.Repeat("k", validate.MaxKeywordChars) + `"],"hasImage":true}`
	rec := do(t, api, http.MethodPost, "/projects", tok, []byte(body))
	if rec.Code != http.StatusCreated {
		t.Fatalf("a body at the caps was refused: %d %s", rec.Code, rec.Body.String())
	}
	var p protocol.ProjectRecord
	json.Unmarshal(rec.Body.Bytes(), &p)
	if p.Name != name || p.Color != "#AbCdEf" {
		t.Fatalf("stored %+v", p)
	}
	if r := do(t, api, http.MethodPut, "/projects/"+p.ID, tok, []byte(`{"version":0,"name":"","color":"","keywords":[]}`)); r.Code != http.StatusOK {
		t.Fatalf("clearing fields was refused: %d %s", r.Code, r.Body.String())
	}
}
