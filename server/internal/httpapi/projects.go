package httpapi

import (
	"errors"
	"net/http"

	"stencil/server/internal/auth"
	"stencil/server/internal/eventbus"
	"stencil/server/internal/protocol"
	"stencil/server/internal/service"
	"stencil/server/internal/store"
)

func (a *API) handleGetProject(rw http.ResponseWriter, req *http.Request) {
	ctx, cancel := a.opCtx(req)
	defer cancel()
	rec, err := a.deps.Projects.GetProject(ctx, req.PathValue("id"))
	if errors.Is(err, store.ErrNotFound) {
		writeNotFound(rw, msgProjectNotFound)
		return
	}
	if err != nil {
		writeInternalError(rw, msgLoadProject)
		return
	}
	resp := protocol.ProjectResponse{Project: rec, Layout: rec.Layout}
	resp.Project.Layout = nil // the layout rides once, beside the record
	writeJSON(rw, http.StatusOK, resp)
}

// handleCreateProject decodes the request; the image rule, the PROJECT_TTL stamping and a legacy inline
// original are policy and live in the service. A refused original answers as its upload would.
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
		writeBadRequest(rw, msgImageRequired)
		return
	case errors.Is(err, service.ErrBadOriginal):
		writeBadRequest(rw, msgBadOriginal)
		return
	case errors.Is(err, service.ErrRecordFile):
		writeInternalError(rw, msgRecordFile)
		return
	case errors.Is(err, service.ErrStoreOriginal):
		writeStoreFileErr(rw, err)
		return
	case err != nil:
		writeInternalError(rw, msgCreateProject)
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
		writeNotFound(rw, msgProjectNotFound)
		return
	case errors.Is(err, store.ErrConflict):
		writeErr(rw, http.StatusConflict, protocol.CodeConflict, msgStaleVersion)
		return
	case err != nil:
		writeInternalError(rw, msgUpdateProject)
		return
	}
	eventbus.PublishProjectEvent(ctx, a.deps.Bus, protocol.EventUpdated, rec)
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
		writeInternalError(rw, msgDeleteProject)
		return
	}
	rw.WriteHeader(http.StatusNoContent)
}
