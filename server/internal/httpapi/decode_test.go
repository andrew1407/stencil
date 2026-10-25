package httpapi

import (
	"encoding/json"
	"net/http"
	"strings"
	"testing"

	"stencil/server/internal/protocol"
)

// A body that will not decode is answered in the API's words — the reason and a quoted, bounded
// field name — never Go's decoder text; a token label over its cap is a 400 too.
func TestDecodeFailuresAndTheLabelCap(t *testing.T) {
	api, _ := testAPI(t, "")
	tok := issueToken(t, api, "")
	for _, tc := range []struct{ body, want string }{
		{`{"name":`, "the body ends early"},
		{`{"name":1}`, `field "name" has the wrong type`},
		{`{"nmae":"x","hasImage":true}`, `unknown field "nmae"`},
		{`{"name" "x"}`, "malformed JSON at byte"},
	} {
		rec := do(t, api, http.MethodPost, "/projects", tok, []byte(tc.body))
		var resp protocol.ErrorResponse
		_ = json.Unmarshal(rec.Body.Bytes(), &resp)
		if rec.Code != http.StatusBadRequest || !strings.Contains(resp.Message, tc.want) || strings.Contains(resp.Message, "json: ") {
			t.Errorf("%s: %d %q, want 400 naming %q", tc.body, rec.Code, resp.Message, tc.want)
		}
	}
	long := `{"label":"` + strings.Repeat("x", 201) + `"}`
	req := do(t, api, http.MethodPost, "/auth/token", testAdmin, []byte(long))
	if req.Code != http.StatusBadRequest {
		t.Errorf("an over-cap label: code %d, want 400", req.Code)
	}
}
