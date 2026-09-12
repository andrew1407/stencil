package httpapi

import (
	"errors"
	"net/http"

	"stencil/server/internal/auth"
	"stencil/server/internal/bus"
	"stencil/server/internal/protocol"
	"stencil/server/internal/service"
	"stencil/server/internal/store"
	"stencil/server/internal/validate"
)

// listPage reads the opt-in keyset paging params; both absent = every project.
func listPage(req *http.Request) (store.ProjectPage, error) {
	q := req.URL.Query()
	page := store.ProjectPage{}
	limit, err := validate.ListLimit(q.Get("limit"))
	if err != nil {
		return page, err
	}
	page.Limit = limit
	after, err := store.ParseProjectCursor(q.Get("after"))
	if err != nil {
		return page, errors.New("after is not a cursor from a previous page")
	}
	page.After = after
	return page, nil
}

func (a *API) handleListProjects(rw http.ResponseWriter, req *http.Request) {
	page, err := listPage(req)
	if err != nil {
		writeErr(rw, http.StatusBadRequest, protocol.CodeBadRequest, err.Error())
		return
	}
	ctx, cancel := a.opCtx(req)
	defer cancel()
	projects, err := a.deps.Projects.ListProjects(ctx, page)
	if err != nil {
		writeErr(rw, http.StatusInternalServerError, protocol.CodeInternal, msgListProjects)
		return
	}
	resp := protocol.ProjectListResponse{Projects: projects}
	// A full page may have more behind it; a short one is the end of the list.
	if page.Limit > 0 && len(projects) == page.Limit {
		last := projects[len(projects)-1]
		resp.NextCursor = store.ProjectCursor{UpdatedAt: last.UpdatedAt, ID: last.ID}.String()
	}
	writeJSON(rw, http.StatusOK, resp)
}

func (a *API) handleGetProject(rw http.ResponseWriter, req *http.Request) {
	ctx, cancel := a.opCtx(req)
	defer cancel()
	rec, err := a.deps.Projects.GetProject(ctx, req.PathValue("id"))
	if errors.Is(err, store.ErrNotFound) {
		writeErr(rw, http.StatusNotFound, protocol.CodeNotFound, msgProjectNotFound)
		return
	}
	if err != nil {
		writeErr(rw, http.StatusInternalServerError, protocol.CodeInternal, msgLoadProject)
		return
	}
	resp := protocol.ProjectResponse{
		Project:         rec,
		Layout:          rec.Layout,
		OriginalContent: rec.OriginalContent,
	}
	// Avoid duplicating the payload inside Project too.
	resp.Project.Layout = nil
	resp.Project.OriginalContent = ""
	writeJSON(rw, http.StatusOK, resp)
}

// handleCreateProject decodes the request; the image rule and the PROJECT_TTL
// stamping are policy and live in the service.
func (a *API) handleCreateProject(rw http.ResponseWriter, req *http.Request) {
	var body protocol.CreateProjectRequest
	if !a.decodeJSON(rw, req, &body) {
		return
	}
	owner := ""
	if sess, ok := auth.SessionFromContext(req.Context()); ok {
		owner = sess.ID
	}
	ctx, cancel := a.opCtx(req)
	defer cancel()
	rec, err := a.projects.Create(ctx, owner, body)
	switch {
	case errors.Is(err, service.ErrImageRequired):
		writeErr(rw, http.StatusBadRequest, protocol.CodeBadRequest, msgImageRequired)
		return
	case err != nil:
		writeErr(rw, http.StatusInternalServerError, protocol.CodeInternal, msgCreateProject)
		return
	}
	writeJSON(rw, http.StatusCreated, rec)
}

func (a *API) handleUpdateProject(rw http.ResponseWriter, req *http.Request) {
	var body protocol.UpdateProjectRequest
	if !a.decodeJSON(rw, req, &body) {
		return
	}
	ctx, cancel := a.opCtx(req)
	defer cancel()
	rec, err := a.deps.Projects.UpdateProject(ctx, req.PathValue("id"), store.ProjectPatch{
		Name:        body.Name,
		Color:       body.Color,
		Description: body.Description,
		Keywords:    body.Keywords,
		BlankColor:  body.BlankColor,
		ExpiresAt:   body.ExpiresAt,
		Layout:      body.Layout,
	}, body.Version)
	switch {
	case errors.Is(err, store.ErrNotFound):
		writeErr(rw, http.StatusNotFound, protocol.CodeNotFound, msgProjectNotFound)
		return
	case errors.Is(err, store.ErrConflict):
		writeErr(rw, http.StatusConflict, protocol.CodeConflict, msgStaleVersion)
		return
	case err != nil:
		writeErr(rw, http.StatusInternalServerError, protocol.CodeInternal, msgUpdateProject)
		return
	}
	bus.PublishProjectEvent(ctx, a.deps.Bus, protocol.EventUpdated, rec)
	writeJSON(rw, http.StatusOK, rec)
}

// handleDeleteProject defers to the service: the live-session guard and the
// row-then-bytes-then-announce ordering are shared with the expiry sweep.
func (a *API) handleDeleteProject(rw http.ResponseWriter, req *http.Request) {
	ctx, cancel := a.opCtx(req)
	defer cancel()
	err := a.projects.Delete(ctx, req.PathValue("id"))
	switch {
	case errors.Is(err, service.ErrProjectInUse):
		writeErr(rw, http.StatusConflict, protocol.CodeConflict, msgProjectInUse)
		return
	case err != nil:
		writeErr(rw, http.StatusInternalServerError, protocol.CodeInternal, msgDeleteProject)
		return
	}
	rw.WriteHeader(http.StatusNoContent)
}
