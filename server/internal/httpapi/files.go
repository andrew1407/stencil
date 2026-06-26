package httpapi

import (
	"errors"
	"io"
	"net/http"
	"path"
	"strconv"
	"strings"

	"stencil/server/internal/filestore"
	"stencil/server/internal/protocol"
	"stencil/server/internal/store"
)

// handleGetFile streams the original or result image bytes for a project. The
// stored path (and thus the extension) is read from the project record, so no
// client-supplied filename is involved.
func (a *API) handleGetFile(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	kind := r.PathValue("kind")
	if kind != protocol.KindOriginal && kind != protocol.KindResult {
		writeErr(w, http.StatusBadRequest, protocol.CodeBadRequest, "unknown file kind")
		return
	}
	rec, err := a.deps.Projects.GetProject(r.Context(), id)
	if errors.Is(err, store.ErrNotFound) {
		writeErr(w, http.StatusNotFound, protocol.CodeNotFound, "project not found")
		return
	}
	if err != nil {
		writeErr(w, http.StatusInternalServerError, protocol.CodeInternal, "could not load project")
		return
	}
	rel := rec.OriginalPath
	if kind == protocol.KindResult {
		rel = rec.ResultPath
	}
	if rel == "" {
		writeErr(w, http.StatusNotFound, protocol.CodeNotFound, "no "+kind+" file")
		return
	}
	data, err := a.deps.Files.GetByRelPath(rel)
	if errors.Is(err, filestore.ErrNotFound) {
		writeErr(w, http.StatusNotFound, protocol.CodeNotFound, "file missing")
		return
	}
	if err != nil {
		writeErr(w, http.StatusInternalServerError, protocol.CodeInternal, "could not read file")
		return
	}
	w.Header().Set("Content-Type", contentTypeForExt(path.Ext(rel)))
	w.Header().Set("Content-Length", strconv.Itoa(len(data)))
	_, _ = w.Write(data)
}

// handlePutFile stores raw image bytes for a project. The extension and (for
// originals) the pixel dimensions are passed as query params, since the server
// is codec-free and never decodes images — clients render and measure.
func (a *API) handlePutFile(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	kind := r.PathValue("kind")
	if kind != protocol.KindOriginal && kind != protocol.KindResult {
		writeErr(w, http.StatusBadRequest, protocol.CodeBadRequest, "unknown file kind")
		return
	}
	ext := strings.TrimPrefix(r.URL.Query().Get("ext"), ".")
	width, _ := strconv.Atoi(r.URL.Query().Get("w"))
	height, _ := strconv.Atoi(r.URL.Query().Get("h"))

	body := http.MaxBytesReader(w, r.Body, a.deps.MaxBodyBytes)
	data, err := io.ReadAll(body)
	if err != nil {
		writeErr(w, http.StatusRequestEntityTooLarge, protocol.CodeBadRequest, "body too large or unreadable")
		return
	}
	if len(data) == 0 {
		writeErr(w, http.StatusBadRequest, protocol.CodeBadRequest, "empty file body")
		return
	}

	// Ensure the project exists before writing bytes for it.
	if _, err := a.deps.Projects.GetProject(r.Context(), id); errors.Is(err, store.ErrNotFound) {
		writeErr(w, http.StatusNotFound, protocol.CodeNotFound, "project not found")
		return
	}

	rel, err := a.deps.Files.Put(id, kind, ext, data)
	if err != nil {
		var unsafe filestore.ErrUnsafePath
		if errors.As(err, &unsafe) {
			writeErr(w, http.StatusBadRequest, protocol.CodeBadRequest, "rejected path")
			return
		}
		writeErr(w, http.StatusInternalServerError, protocol.CodeInternal, "could not store file")
		return
	}
	rec, err := a.deps.Projects.SetFile(r.Context(), id, kind, rel, width, height)
	if err != nil {
		writeErr(w, http.StatusInternalServerError, protocol.CodeInternal, "could not record file")
		return
	}
	a.publishEvent(r.Context(), protocol.EventUpdated, rec)
	writeJSON(w, http.StatusCreated, protocol.FileWriteResponse{Path: rel, W: width, H: height})
}

// contentTypeForExt maps a file extension to a content type without decoding.
func contentTypeForExt(ext string) string {
	switch strings.ToLower(strings.TrimPrefix(ext, ".")) {
	case "png":
		return "image/png"
	case "jpg", "jpeg":
		return "image/jpeg"
	case "webp":
		return "image/webp"
	case "gif":
		return "image/gif"
	case "bmp":
		return "image/bmp"
	default:
		return "application/octet-stream"
	}
}
