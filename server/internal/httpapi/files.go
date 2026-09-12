package httpapi

import (
	"context"
	"errors"
	"net/http"
	"path"
	"strconv"
	"strings"
	"sync"
	"time"

	"stencil/server/internal/filestore"
	"stencil/server/internal/protocol"
	"stencil/server/internal/service"
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
		writeErr(w, http.StatusBadRequest, protocol.CodeBadRequest, msgUnknownFileKind)
		return
	}
	ctx, cancel := a.opCtx(r)
	defer cancel()
	got := a.lookupFile(ctx, id, kind)
	if errors.Is(got.recErr, store.ErrNotFound) {
		writeErr(w, http.StatusNotFound, protocol.CodeNotFound, msgProjectNotFound)
		return
	}
	if got.recErr != nil {
		writeErr(w, http.StatusInternalServerError, protocol.CodeInternal, msgLoadProject)
		return
	}
	rel := got.rel
	switch kind {
	case protocol.KindOriginal:
		rel = got.rec.OriginalPath
	case protocol.KindResult:
		rel = got.rec.ResultPath
	default:
		if got.relErr != nil && !errors.Is(got.relErr, filestore.ErrNotFound) {
			writeErr(w, http.StatusInternalServerError, protocol.CodeInternal, msgListFiles)
			return
		}
	}
	if rel == "" {
		writeErr(w, http.StatusNotFound, protocol.CodeNotFound, msgNoFileOfKind(kind))
		return
	}
	f, err := a.deps.Files.OpenByRelPath(rel)
	if errors.Is(err, filestore.ErrNotFound) {
		writeErr(w, http.StatusNotFound, protocol.CodeNotFound, msgFileMissing)
		return
	}
	if err != nil {
		writeErr(w, http.StatusInternalServerError, protocol.CodeInternal, msgReadFile)
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

// fileLookup holds the two reads a download needs. Each goroutine below writes
// its own fields and wg.Wait orders them against the reader.
type fileLookup struct {
	rec    protocol.ProjectRecord
	recErr error
	rel    string
	relErr error
}

// lookupFile reads the project row and, for a filestore-only kind, the stored
// path — concurrently, because only original/result take their path from the row.
// The row is still read for every kind: a missing project must answer 404 even
// when its bytes are on disk, so recErr outranks relErr at the call site.
func (a *API) lookupFile(ctx context.Context, id, kind string) fileLookup {
	var (
		out fileLookup
		wg  sync.WaitGroup
	)
	wg.Add(1)
	go func() {
		defer wg.Done()
		out.rec, out.recErr = a.deps.Projects.GetProject(ctx, id)
	}()
	if protocol.IsFilestoreOnlyKind(kind) {
		wg.Add(1)
		go func() {
			defer wg.Done()
			out.rel, out.relErr = a.deps.Files.FindByKind(id, kind)
		}()
	}
	wg.Wait()
	return out
}

// handlePutFile stores raw image bytes for a project. The extension and (for
// originals) the pixel dimensions are passed as query params, since the server
// is codec-free and never decodes images — clients render and measure. The
// three-step, two-store write itself (and the compensating deletes that undo a
// half-done one) is the service's saga.
func (a *API) handlePutFile(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	kind := r.PathValue("kind")
	if !protocol.IsFileKind(kind) {
		writeErr(w, http.StatusBadRequest, protocol.CodeBadRequest, msgUnknownFileKind)
		return
	}
	ctx, cancel := a.opCtx(r)
	defer cancel()
	put := service.FilePut{ID: id, Kind: kind, Ext: strings.TrimPrefix(r.URL.Query().Get("ext"), ".")}
	put.W, _ = strconv.Atoi(r.URL.Query().Get("w"))
	put.H, _ = strconv.Atoi(r.URL.Query().Get("h"))

	// The body streams straight to disk: a 32 MiB upload never sits in the heap.
	resp, err := a.files.Store(ctx, put, http.MaxBytesReader(w, r.Body, a.deps.MaxBodyBytes))
	switch {
	case errors.Is(err, store.ErrNotFound):
		writeErr(w, http.StatusNotFound, protocol.CodeNotFound, msgProjectNotFound)
		return
	case errors.Is(err, service.ErrRecordFile):
		writeErr(w, http.StatusInternalServerError, protocol.CodeInternal, msgRecordFile)
		return
	case err != nil:
		writeStoreFileErr(w, err)
		return
	}
	writeJSON(w, http.StatusCreated, resp)
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
		writeErr(w, http.StatusBadRequest, protocol.CodeBadRequest, msgUnknownFileKind)
		return
	}
	if !protocol.IsFilestoreOnlyKind(kind) {
		writeErr(w, http.StatusBadRequest, protocol.CodeBadRequest, msgKindGoesWithProject(kind))
		return
	}
	ctx, cancel := a.opCtx(r)
	defer cancel()
	if _, err := a.deps.Projects.GetProject(ctx, id); errors.Is(err, store.ErrNotFound) {
		writeErr(w, http.StatusNotFound, protocol.CodeNotFound, msgProjectNotFound)
		return
	} else if err != nil {
		writeErr(w, http.StatusInternalServerError, protocol.CodeInternal, msgLoadProject)
		return
	}
	if err := a.deps.Files.RemoveKind(id, kind); err != nil {
		writeErr(w, http.StatusInternalServerError, protocol.CodeInternal, msgDeleteFile)
		return
	}
	w.WriteHeader(http.StatusNoContent)
}

// writeStoreFileErr maps one failed upload onto its response.
func writeStoreFileErr(w http.ResponseWriter, err error) {
	var unsafe filestore.ErrUnsafePath
	var tooBig *http.MaxBytesError
	switch {
	case errors.As(err, &tooBig):
		writeErr(w, http.StatusRequestEntityTooLarge, protocol.CodeBadRequest, msgBodyTooLarge)
	case errors.Is(err, filestore.ErrEmpty):
		writeErr(w, http.StatusBadRequest, protocol.CodeBadRequest, msgEmptyBody)
	case errors.As(err, &unsafe):
		writeErr(w, http.StatusBadRequest, protocol.CodeBadRequest, msgRejectedPath)
	case errors.Is(err, filestore.ErrQuotaExceeded):
		// STORAGE_QUOTA_BYTES: the server is full, not the request malformed.
		writeErr(w, http.StatusInsufficientStorage, protocol.CodeInternal, msgQuotaExceeded)
	default:
		writeErr(w, http.StatusInternalServerError, protocol.CodeInternal, msgStoreFile)
	}
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
