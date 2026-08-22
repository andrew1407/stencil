package httpapi

import (
	"errors"
	"io"
	"net/http"
	"path"
	"strconv"
	"strings"
	"time"

	"stencil/server/internal/filestore"
	"stencil/server/internal/protocol"
	"stencil/server/internal/store"
)

// handleGetFile streams a project file's bytes. For original/result the stored
// path (and thus the extension) is read from the project record; video/variant
// kinds are filestore-only in v1 (llm-contract.md §9), so their path is
// resolved by the filestore's own kind lookup. No client-supplied filename is
// ever involved. Bytes are served via http.ServeContent over the confined
// *os.File, so large files (video) stream instead of being read fully into
// memory per request, and Range requests work for free.
func (a *API) handleGetFile(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	kind := r.PathValue("kind")
	if !protocol.IsFileKind(kind) {
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
	var rel string
	switch kind {
	case protocol.KindOriginal:
		rel = rec.OriginalPath
	case protocol.KindResult:
		rel = rec.ResultPath
	default:
		rel, err = a.deps.Files.FindByKind(id, kind)
		if err != nil && !errors.Is(err, filestore.ErrNotFound) {
			writeErr(w, http.StatusInternalServerError, protocol.CodeInternal, "could not list files")
			return
		}
	}
	if rel == "" {
		writeErr(w, http.StatusNotFound, protocol.CodeNotFound, "no "+kind+" file")
		return
	}
	f, err := a.deps.Files.OpenByRelPath(rel)
	if errors.Is(err, filestore.ErrNotFound) {
		writeErr(w, http.StatusNotFound, protocol.CodeNotFound, "file missing")
		return
	}
	if err != nil {
		writeErr(w, http.StatusInternalServerError, protocol.CodeInternal, "could not read file")
		return
	}
	defer f.Close()
	w.Header().Set("Content-Type", contentTypeForExt(path.Ext(rel)))
	// Client-uploaded bytes served from our own origin: pin the declared type, or a
	// browser may sniff an "image" as something scriptable and run it here.
	w.Header().Set("X-Content-Type-Options", "nosniff")
	// The zero modtime suppresses Last-Modified/conditional handling, keeping
	// the response shape otherwise unchanged (plus Accept-Ranges).
	http.ServeContent(w, r, "", time.Time{}, f)
}

// handlePutFile stores raw image bytes for a project. The extension and (for
// originals) the pixel dimensions are passed as query params, since the server
// is codec-free and never decodes images — clients render and measure.
func (a *API) handlePutFile(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	kind := r.PathValue("kind")
	if !protocol.IsFileKind(kind) {
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
		if errors.Is(err, filestore.ErrQuotaExceeded) {
			// STORAGE_QUOTA_BYTES: the server is full, not the request malformed.
			writeErr(w, http.StatusInsufficientStorage, protocol.CodeInternal, "server storage quota exceeded")
			return
		}
		writeErr(w, http.StatusInternalServerError, protocol.CodeInternal, "could not store file")
		return
	}
	// Only original/result update the project record; video/variantN bytes are
	// filestore-only in v1 (llm-contract.md §9) and are removed with the
	// project. Either way, a project row deleted mid-upload (e.g. the expiry
	// sweep ran after the existence check above) must not orphan the bytes we
	// just wrote: drop them and report gone. RemoveKind, not Remove — the latter
	// deletes the whole project directory over a race on one upload.
	if kind == protocol.KindOriginal || kind == protocol.KindResult {
		rec, err := a.deps.Projects.SetFile(r.Context(), id, kind, rel, width, height)
		if err != nil {
			if errors.Is(err, store.ErrNotFound) {
				_ = a.deps.Files.RemoveKind(id, kind)
				writeErr(w, http.StatusNotFound, protocol.CodeNotFound, "project not found")
				return
			}
			writeErr(w, http.StatusInternalServerError, protocol.CodeInternal, "could not record file")
			return
		}
		a.publishEvent(r.Context(), protocol.EventUpdated, rec)
	} else if _, err := a.deps.Projects.GetProject(r.Context(), id); errors.Is(err, store.ErrNotFound) {
		_ = a.deps.Files.RemoveKind(id, kind)
		writeErr(w, http.StatusNotFound, protocol.CodeNotFound, "project not found")
		return
	}
	writeJSON(w, http.StatusCreated, protocol.FileWriteResponse{Path: rel, W: width, H: height})
}

// handleDeleteFile removes the stored bytes for a filestore-only kind
// (video/variantN/chat — llm-contract.md §9). original/result are part of
// the project record and are only removed with the project, so deleting them
// here is rejected. Deleting a kind with no stored bytes still answers 204
// (idempotent), and a file delete never bumps the project version.
func (a *API) handleDeleteFile(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	kind := r.PathValue("kind")
	if !protocol.IsFileKind(kind) {
		writeErr(w, http.StatusBadRequest, protocol.CodeBadRequest, "unknown file kind")
		return
	}
	if !protocol.IsFilestoreOnlyKind(kind) {
		writeErr(w, http.StatusBadRequest, protocol.CodeBadRequest, kind+" is removed with the project")
		return
	}
	if _, err := a.deps.Projects.GetProject(r.Context(), id); errors.Is(err, store.ErrNotFound) {
		writeErr(w, http.StatusNotFound, protocol.CodeNotFound, "project not found")
		return
	} else if err != nil {
		writeErr(w, http.StatusInternalServerError, protocol.CodeInternal, "could not load project")
		return
	}
	if err := a.deps.Files.RemoveKind(id, kind); err != nil {
		writeErr(w, http.StatusInternalServerError, protocol.CodeInternal, "could not delete file")
		return
	}
	w.WriteHeader(http.StatusNoContent)
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
	case "json":
		return "application/json"
	default:
		return "application/octet-stream"
	}
}
