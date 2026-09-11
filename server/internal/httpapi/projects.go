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
func listPage(r *http.Request) (store.ProjectPage, error) {
	q := r.URL.Query()
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

func (a *API) handleListProjects(w http.ResponseWriter, r *http.Request) {
	page, err := listPage(r)
	if err != nil {
		writeErr(w, http.StatusBadRequest, protocol.CodeBadRequest, err.Error())
		return
	}
	ctx, cancel := a.opCtx(r)
	defer cancel()
	projects, err := a.deps.Projects.ListProjects(ctx, page)
	if err != nil {
		writeErr(w, http.StatusInternalServerError, protocol.CodeInternal, msgListProjects)
		return
	}
	resp := protocol.ProjectListResponse{Projects: projects}
	// A full page may have more behind it; a short one is the end of the list.
	if page.Limit > 0 && len(projects) == page.Limit {
		last := projects[len(projects)-1]
		resp.NextCursor = store.ProjectCursor{UpdatedAt: last.UpdatedAt, ID: last.ID}.String()
	}
	writeJSON(w, http.StatusOK, resp)
}

func (a *API) handleGetProject(w http.ResponseWriter, r *http.Request) {
	ctx, cancel := a.opCtx(r)
	defer cancel()
	rec, err := a.deps.Projects.GetProject(ctx, r.PathValue("id"))
	if errors.Is(err, store.ErrNotFound) {
		writeErr(w, http.StatusNotFound, protocol.CodeNotFound, msgProjectNotFound)
		return
	}
	if err != nil {
		writeErr(w, http.StatusInternalServerError, protocol.CodeInternal, msgLoadProject)
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
	writeJSON(w, http.StatusOK, resp)
}

// handleCreateProject decodes the request; the image rule and the PROJECT_TTL
// stamping are policy and live in the service.
func (a *API) handleCreateProject(w http.ResponseWriter, r *http.Request) {
	var req protocol.CreateProjectRequest
	if !a.decodeJSON(w, r, &req) {
		return
	}
	owner := ""
	if sess, ok := auth.SessionFromContext(r.Context()); ok {
		owner = sess.ID
	}
	ctx, cancel := a.opCtx(r)
	defer cancel()
	rec, err := a.projects.Create(ctx, owner, req)
	switch {
	case errors.Is(err, service.ErrImageRequired):
		writeErr(w, http.StatusBadRequest, protocol.CodeBadRequest, msgImageRequired)
		return
	case err != nil:
		writeErr(w, http.StatusInternalServerError, protocol.CodeInternal, msgCreateProject)
		return
	}
	writeJSON(w, http.StatusCreated, rec)
}

func (a *API) handleUpdateProject(w http.ResponseWriter, r *http.Request) {
	var req protocol.UpdateProjectRequest
	if !a.decodeJSON(w, r, &req) {
		return
	}
	ctx, cancel := a.opCtx(r)
	defer cancel()
	rec, err := a.deps.Projects.UpdateProject(ctx, r.PathValue("id"), store.ProjectPatch{
		Name:        req.Name,
		Color:       req.Color,
		Description: req.Description,
		Keywords:    req.Keywords,
		BlankColor:  req.BlankColor,
		ExpiresAt:   req.ExpiresAt,
		Layout:      req.Layout,
	}, req.Version)
	switch {
	case errors.Is(err, store.ErrNotFound):
		writeErr(w, http.StatusNotFound, protocol.CodeNotFound, msgProjectNotFound)
		return
	case errors.Is(err, store.ErrConflict):
		writeErr(w, http.StatusConflict, protocol.CodeConflict, msgStaleVersion)
		return
	case err != nil:
		writeErr(w, http.StatusInternalServerError, protocol.CodeInternal, msgUpdateProject)
		return
	}
	bus.PublishProjectEvent(ctx, a.deps.Bus, protocol.EventUpdated, rec)
	writeJSON(w, http.StatusOK, rec)
}

// handleDeleteProject defers to the service: the live-session guard and the
// row-then-bytes-then-announce ordering are shared with the expiry sweep.
func (a *API) handleDeleteProject(w http.ResponseWriter, r *http.Request) {
	ctx, cancel := a.opCtx(r)
	defer cancel()
	err := a.projects.Delete(ctx, r.PathValue("id"))
	switch {
	case errors.Is(err, service.ErrProjectInUse):
		writeErr(w, http.StatusConflict, protocol.CodeConflict, msgProjectInUse)
		return
	case err != nil:
		writeErr(w, http.StatusInternalServerError, protocol.CodeInternal, msgDeleteProject)
		return
	}
	w.WriteHeader(http.StatusNoContent)
}
