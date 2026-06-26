package httpapi

import (
	"context"
	"encoding/json"
	"errors"
	"net/http"

	"stencil/server/internal/auth"
	"stencil/server/internal/bus"
	"stencil/server/internal/protocol"
	"stencil/server/internal/store"
)

func (a *API) handleListProjects(w http.ResponseWriter, r *http.Request) {
	projects, err := a.deps.Projects.ListProjects(r.Context())
	if err != nil {
		writeErr(w, http.StatusInternalServerError, protocol.CodeInternal, "could not list projects")
		return
	}
	writeJSON(w, http.StatusOK, protocol.ProjectListResponse{Projects: projects})
}

func (a *API) handleGetProject(w http.ResponseWriter, r *http.Request) {
	rec, err := a.deps.Projects.GetProject(r.Context(), r.PathValue("id"))
	if errors.Is(err, store.ErrNotFound) {
		writeErr(w, http.StatusNotFound, protocol.CodeNotFound, "project not found")
		return
	}
	if err != nil {
		writeErr(w, http.StatusInternalServerError, protocol.CodeInternal, "could not load project")
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

func (a *API) handleCreateProject(w http.ResponseWriter, r *http.Request) {
	var req protocol.CreateProjectRequest
	if !a.decodeJSON(w, r, &req) {
		return
	}
	owner := ""
	if sess, ok := auth.SessionFromContext(r.Context()); ok {
		owner = sess.ID
	}
	rec, err := a.deps.Projects.CreateProject(r.Context(), owner, req)
	if err != nil {
		writeErr(w, http.StatusInternalServerError, protocol.CodeInternal, "could not create project")
		return
	}
	a.publishEvent(r.Context(), protocol.EventCreated, rec)
	writeJSON(w, http.StatusCreated, rec)
}

func (a *API) handleUpdateProject(w http.ResponseWriter, r *http.Request) {
	var req protocol.UpdateProjectRequest
	if !a.decodeJSON(w, r, &req) {
		return
	}
	rec, err := a.deps.Projects.UpdateProject(r.Context(), r.PathValue("id"), req.Name, req.Layout, req.Version)
	switch {
	case errors.Is(err, store.ErrNotFound):
		writeErr(w, http.StatusNotFound, protocol.CodeNotFound, "project not found")
		return
	case errors.Is(err, store.ErrConflict):
		writeErr(w, http.StatusConflict, protocol.CodeConflict, "stale version; reload and retry")
		return
	case err != nil:
		writeErr(w, http.StatusInternalServerError, protocol.CodeInternal, "could not update project")
		return
	}
	a.publishEvent(r.Context(), protocol.EventUpdated, rec)
	writeJSON(w, http.StatusOK, rec)
}

func (a *API) handleDeleteProject(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	if err := a.deps.Projects.DeleteProject(r.Context(), id); err != nil {
		writeErr(w, http.StatusInternalServerError, protocol.CodeInternal, "could not delete project")
		return
	}
	if a.deps.Files != nil {
		_ = a.deps.Files.Remove(id)
	}
	a.publishEvent(r.Context(), protocol.EventDeleted, protocol.ProjectRecord{ID: id})
	w.WriteHeader(http.StatusNoContent)
}

// publishEvent broadcasts a project-lifecycle event to the global feed so every
// connected client refreshes its projects list live. Best-effort: a bus error
// never fails the request.
func (a *API) publishEvent(ctx context.Context, event string, rec protocol.ProjectRecord) {
	if a.deps.Bus == nil {
		return
	}
	msg := protocol.WSMessage{Type: protocol.WSProjectEv, Event: event, Project: &rec}
	data, err := json.Marshal(msg)
	if err != nil {
		return
	}
	_ = a.deps.Bus.Publish(ctx, bus.ChannelEvents, data)
}
