package httpapi

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"net/http"
	"strings"

	"stencil/server/internal/protocol"
	"stencil/server/internal/store"
	"stencil/server/internal/validate"
)

// listPage reads the keyset paging params; with no ?limit= the page is Deps.ProjectsPageSize (0 = all).
func (a *API) listPage(req *http.Request) (store.ProjectPage, error) {
	q := req.URL.Query()
	page := store.ProjectPage{Limit: a.deps.ProjectsPageSize}
	limit, err := validate.ListLimit(q.Get("limit"))
	if err != nil {
		return page, err
	}
	if limit > 0 {
		page.Limit = limit
	}
	after, err := store.ParseProjectCursor(q.Get("after"))
	if err != nil {
		return page, errors.New("after is not a cursor from a previous page")
	}
	page.After = after
	return page, nil
}

// handleListProjects answers one page with a strong ETag over its body: a poller that sends it back as
// If-None-Match gets 304 and no body while the list is unchanged.
func (a *API) handleListProjects(rw http.ResponseWriter, req *http.Request) {
	page, err := a.listPage(req)
	if err != nil {
		writeBadRequest(rw, err.Error())
		return
	}
	ctx, cancel := a.opCtx(req)
	defer cancel()
	projects, err := a.deps.Projects.ListProjects(ctx, page)
	if err != nil {
		writeInternalError(rw, msgListProjects)
		return
	}
	resp := protocol.ProjectListResponse{Projects: projects}
	// A full page may have more behind it; a short one is the end of the list.
	if page.Limit > 0 && len(projects) == page.Limit {
		last := projects[len(projects)-1]
		resp.NextCursor = store.ProjectCursor{UpdatedAt: last.UpdatedAt, ID: last.ID}.String()
	}
	body, err := json.Marshal(resp)
	if err != nil {
		writeInternalError(rw, msgListProjects)
		return
	}
	sum := sha256.Sum256(body)
	etag := `"` + hex.EncodeToString(sum[:16]) + `"`
	rw.Header().Set("ETag", etag)
	rw.Header().Set("Cache-Control", "private, no-cache") // revalidate every time; never a shared cache
	if etagMatches(req.Header.Get("If-None-Match"), etag) {
		rw.WriteHeader(http.StatusNotModified)
		return
	}
	rw.Header().Set("Content-Type", "application/json")
	rw.WriteHeader(http.StatusOK)
	_, _ = rw.Write(append(body, '\n')) // the byte shape writeJSON's encoder gives every other route
}

// etagMatches applies If-None-Match's weak comparison: any listed tag, W/ or not, or "*".
func etagMatches(header, etag string) bool {
	for _, tag := range strings.Split(header, ",") {
		tag = strings.TrimPrefix(strings.TrimSpace(tag), "W/")
		if tag == etag || tag == "*" {
			return true
		}
	}
	return false
}
