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

// handleGetFile streams a project file: original/result take their path from the project record, the
// filestore-only kinds (§9) from the filestore's own kind lookup. ServeContent gives Range for free.
func (a *API) handleGetFile(rw http.ResponseWriter, req *http.Request) {
	id := req.PathValue("id")
	kind := req.PathValue("kind")
	if !protocol.IsFileKind(kind) {
		writeBadRequest(rw, msgUnknownFileKind)
		return
	}
	ctx, cancel := a.opCtx(req)
	defer cancel()
	got := a.lookupFile(ctx, id, kind)
	if errors.Is(got.recErr, store.ErrNotFound) {
		writeNotFound(rw, msgProjectNotFound)
		return
	}
	if got.recErr != nil {
		writeInternalError(rw, msgLoadProject)
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
			writeInternalError(rw, msgListFiles)
			return
		}
	}
	if rel == "" {
		writeNotFound(rw, msgNoFileOfKind(kind))
		return
	}
	f, err := a.deps.Files.OpenByRelPath(rel)
	if errors.Is(err, filestore.ErrNotFound) {
		writeNotFound(rw, msgFileMissing)
		return
	}
	if err != nil {
		writeInternalError(rw, msgReadFile)
		return
	}
	defer f.Close()
	rw.Header().Set("Content-Type", contentTypeForExt(path.Ext(rel)))
	// Client-uploaded bytes served from our own origin: pin the declared type, or a
	// browser may sniff an "image" as something scriptable and run it here.
	rw.Header().Set("X-Content-Type-Options", "nosniff")
	// The zero modtime suppresses Last-Modified/conditional handling, keeping
	// the response shape otherwise unchanged (plus Accept-Ranges).
	http.ServeContent(rw, req, "", time.Time{}, f)
}

// fileLookup holds the two reads a download needs. Each goroutine below writes
// its own fields and wg.Wait orders them against the reader.
type fileLookup struct {
	rec    protocol.ProjectRecord
	recErr error
	rel    string
	relErr error
}

// lookupFile reads the project row and, for a filestore-only kind, the stored path concurrently. The row
// is read for every kind: a missing project must answer 404, so recErr outranks relErr at the call site.
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

// handlePutFile stores raw image bytes; the extension and (for originals) the pixel dimensions arrive as
// query params, since the server is codec-free and never decodes images — clients render and measure.
func (a *API) handlePutFile(rw http.ResponseWriter, req *http.Request) {
	id := req.PathValue("id")
	kind := req.PathValue("kind")
	if !protocol.IsFileKind(kind) {
		writeBadRequest(rw, msgUnknownFileKind)
		return
	}
	ctx, cancel := a.opCtx(req)
	defer cancel()
	put := service.FilePut{ID: id, Kind: kind, Ext: strings.TrimPrefix(req.URL.Query().Get("ext"), ".")}
	put.W, _ = strconv.Atoi(req.URL.Query().Get("w"))
	put.H, _ = strconv.Atoi(req.URL.Query().Get("h"))

	// The body streams straight to disk: a 32 MiB upload never sits in the heap.
	resp, err := a.files.Store(ctx, put, http.MaxBytesReader(rw, req.Body, a.deps.MaxBodyBytes))
	switch {
	case errors.Is(err, store.ErrNotFound):
		writeNotFound(rw, msgProjectNotFound)
		return
	case errors.Is(err, service.ErrRecordFile):
		writeInternalError(rw, msgRecordFile)
		return
	case err != nil:
		writeStoreFileErr(rw, err)
		return
	}
	writeJSON(rw, http.StatusCreated, resp)
}

// handleDeleteFile removes the bytes of a filestore-only kind (video/variantN/chat — §9); original/result
// belong to the project record and are rejected. An absent kind still answers 204, and no version bumps.
func (a *API) handleDeleteFile(rw http.ResponseWriter, req *http.Request) {
	id := req.PathValue("id")
	kind := req.PathValue("kind")
	if !protocol.IsFileKind(kind) {
		writeBadRequest(rw, msgUnknownFileKind)
		return
	}
	if !protocol.IsFilestoreOnlyKind(kind) {
		writeBadRequest(rw, msgKindGoesWithProject(kind))
		return
	}
	ctx, cancel := a.opCtx(req)
	defer cancel()
	if _, err := a.deps.Projects.GetProject(ctx, id); errors.Is(err, store.ErrNotFound) {
		writeNotFound(rw, msgProjectNotFound)
		return
	} else if err != nil {
		writeInternalError(rw, msgLoadProject)
		return
	}
	if err := a.deps.Files.RemoveKind(id, kind); err != nil {
		writeInternalError(rw, msgDeleteFile)
		return
	}
	rw.WriteHeader(http.StatusNoContent)
}

// writeStoreFileErr maps one failed upload onto its response.
func writeStoreFileErr(rw http.ResponseWriter, err error) {
	var unsafe filestore.ErrUnsafePath
	var tooBig *http.MaxBytesError
	switch {
	case errors.As(err, &tooBig):
		writeErr(rw, http.StatusRequestEntityTooLarge, protocol.CodeBadRequest, msgBodyTooLarge)
	case errors.Is(err, filestore.ErrEmpty):
		writeBadRequest(rw, msgEmptyBody)
	case errors.As(err, &unsafe):
		writeBadRequest(rw, msgRejectedPath)
	case errors.Is(err, filestore.ErrQuotaExceeded):
		// STORAGE_QUOTA_BYTES: the server is full, not the request malformed.
		writeErr(rw, http.StatusInsufficientStorage, protocol.CodeInternal, msgQuotaExceeded)
	default:
		writeInternalError(rw, msgStoreFile)
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
