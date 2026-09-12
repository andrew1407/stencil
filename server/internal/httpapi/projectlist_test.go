package httpapi

import (
	"encoding/json"
	"net/http"
	"strconv"
	"strings"
	"testing"

	"stencil/server/internal/protocol"
	"stencil/server/internal/testutil"
)

// seedProjects fills the store with n projects, newest-updated last.
func seedProjects(st *testutil.MemStore, n int) {
	for i := 0; i < n; i++ {
		st.Seed(protocol.ProjectRecord{
			ID: "p_seed" + strconv.Itoa(i) + "_a", Name: "P", UpdatedAt: int64(100 + i),
			OriginalContent: "payload", Layout: json.RawMessage(`{"lines":[]}`)})
	}
}

func listProjects(t *testing.T, api *API, tok, query string) protocol.ProjectListResponse {
	t.Helper()
	rec := do(t, api, http.MethodGet, "/projects"+query, tok, nil)
	if rec.Code != http.StatusOK {
		t.Fatalf("GET /projects%s: code %d body %s", query, rec.Code, rec.Body.String())
	}
	var resp protocol.ProjectListResponse
	if err := json.Unmarshal(rec.Body.Bytes(), &resp); err != nil {
		t.Fatal(err)
	}
	return resp
}

// Paging is opt-in: with no query the response is exactly what it always was —
// every project, no cursor field.
func TestListProjectsDefaultShapeUnchanged(t *testing.T) {
	api, st := testAPI(t, "")
	tok := issueToken(t, api, "")
	seedProjects(st, 5)

	resp := listProjects(t, api, tok, "")
	if len(resp.Projects) != 5 || resp.NextCursor != "" {
		t.Fatalf("default list: %d projects, cursor %q", len(resp.Projects), resp.NextCursor)
	}
	body := do(t, api, http.MethodGet, "/projects", tok, nil).Body.String()
	for _, absent := range []string{"nextCursor", "originalContent", "layout"} {
		if strings.Contains(body, absent) {
			t.Fatalf("list body should not carry %q: %s", absent, body)
		}
	}
}

// ?limit= walks the list in pages, handing back a cursor until the last one.
func TestListProjectsKeysetPages(t *testing.T) {
	api, st := testAPI(t, "")
	tok := issueToken(t, api, "")
	seedProjects(st, 5)

	var ids []string
	query := "?limit=2"
	for pages := 0; ; pages++ {
		if pages > 5 {
			t.Fatal("paging did not terminate")
		}
		resp := listProjects(t, api, tok, query)
		for _, p := range resp.Projects {
			ids = append(ids, p.ID)
		}
		if resp.NextCursor == "" {
			break
		}
		query = "?limit=2&after=" + resp.NextCursor
	}
	if len(ids) != 5 {
		t.Fatalf("paged walk saw %d projects: %v", len(ids), ids)
	}
	// Newest-updated first, no repeats.
	seen := map[string]bool{}
	for i, id := range ids {
		if seen[id] {
			t.Fatalf("project %s repeated across pages", id)
		}
		seen[id] = true
		if want := "p_seed" + strconv.Itoa(4-i) + "_a"; id != want {
			t.Fatalf("position %d is %s, want %s", i, id, want)
		}
	}
}

func TestListProjectsRejectsBadPagingParams(t *testing.T) {
	api, st := testAPI(t, "")
	tok := issueToken(t, api, "")
	seedProjects(st, 2)
	for _, q := range []string{"?limit=0", "?limit=-1", "?limit=nope", "?limit=501", "?after=nonsense"} {
		if rec := do(t, api, http.MethodGet, "/projects"+q, tok, nil); rec.Code != http.StatusBadRequest {
			t.Errorf("GET /projects%s: code %d, want 400", q, rec.Code)
		}
	}
}
