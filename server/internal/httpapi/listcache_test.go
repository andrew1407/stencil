package httpapi

// GET /projects: the configurable default page and the ETag a poller revalidates with.

import (
	"net/http"
	"net/http/httptest"
	"testing"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/filestore"
	"stencil/server/internal/testutil"
)

func getList(t *testing.T, api *API, tok, ifNoneMatch string) *httptest.ResponseRecorder {
	t.Helper()
	req := httptest.NewRequest(http.MethodGet, "/projects", nil)
	req.Header.Set("Authorization", "Bearer "+tok)
	if ifNoneMatch != "" {
		req.Header.Set("If-None-Match", ifNoneMatch)
	}
	rec := httptest.NewRecorder()
	api.Handler().ServeHTTP(rec, req)
	return rec
}

// An unchanged list answers 304 with no body; any change moves the tag.
func TestListProjectsRevalidatesByETag(t *testing.T) {
	api, st := testAPI(t, "")
	tok := issueToken(t, api, "")
	seedProjects(st, 3)
	first := getList(t, api, tok, "")
	etag := first.Header().Get("ETag")
	if first.Code != http.StatusOK || etag == "" || first.Header().Get("Cache-Control") != "private, no-cache" {
		t.Fatalf("first list: %d etag %q headers %v", first.Code, etag, first.Header())
	}
	for _, tag := range []string{etag, "W/" + etag, `"other", ` + etag, "*"} {
		if rec := getList(t, api, tok, tag); rec.Code != http.StatusNotModified || rec.Body.Len() != 0 {
			t.Fatalf("If-None-Match %s: %d with %d body bytes, want an empty 304", tag, rec.Code, rec.Body.Len())
		}
	}
	seedProjects(st, 4)
	if rec := getList(t, api, tok, etag); rec.Code != http.StatusOK || rec.Header().Get("ETag") == etag {
		t.Fatalf("a changed list must answer 200 under a new tag, got %d", rec.Code)
	}
}

// PROJECTS_PAGE_SIZE pages a bare list (with a cursor), and an explicit ?limit= still wins.
// A walk that sends only ?after=, as every client's does, still reaches every row once.
func TestListProjectsDefaultPageSize(t *testing.T) {
	fs, err := filestore.New(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	st := testutil.NewMemStore()
	api := New(Deps{Projects: st, Sessions: st, Files: fs, Bus: eventbus.NewInProc(),
		AdminToken: testAdmin, ProjectsPageSize: 2})
	tok := issueToken(t, api, "")
	seedProjects(st, 5)
	if resp := listProjects(t, api, tok, ""); len(resp.Projects) != 2 || resp.NextCursor == "" {
		t.Fatalf("bare list: %d rows, cursor %q; want a 2-row page", len(resp.Projects), resp.NextCursor)
	}
	if resp := listProjects(t, api, tok, "?limit=4"); len(resp.Projects) != 4 {
		t.Fatalf("?limit=4 gave %d rows", len(resp.Projects))
	}
	seen, pages := map[string]bool{}, 0
	for query := ""; ; pages++ {
		if pages > 5 {
			t.Fatal("a bare ?after= walk did not terminate")
		}
		resp := listProjects(t, api, tok, query)
		for _, p := range resp.Projects {
			if seen[p.ID] {
				t.Fatalf("project %s repeated across pages", p.ID)
			}
			seen[p.ID] = true
		}
		if resp.NextCursor == "" {
			break
		}
		query = "?after=" + resp.NextCursor
	}
	if len(seen) != 5 || pages != 2 {
		t.Fatalf("bare walk saw %d projects over %d pages; want 5 over 3", len(seen), pages+1)
	}
}
