package service

// The upload saga. Storing one file spans two independent stores and cannot be
// wrapped in a transaction, so the compensating deletes live here with the
// steps they undo — not in whichever handler happens to call them.

import (
	"context"
	"fmt"
	"io"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/protocol"
)

// FileService owns writing a project file.
type FileService struct {
	Projects ProjectStore
	Files    UploadFiles
	Bus      eventbus.Bus
}

// NewFiles builds the service.
func NewFiles(projects ProjectStore, files UploadFiles, b eventbus.Bus) *FileService {
	return &FileService{Projects: projects, Files: files, Bus: b}
}

// FilePut names one upload. The server is codec-free and never decodes images,
// so the extension and (for originals) the pixel dimensions come from the caller.
type FilePut struct {
	ID, Kind, Ext string
	W, H          int
}

// Store writes body as the project's bytes for one kind.
//
// Three steps over two stores: check the project exists, write the bytes, then
// record them. Only original/result touch the project record; video/variantN/chat
// bytes are filestore-only in v1 (llm-contract.md §9) and are removed with the
// project, so those re-check instead. Either way a project row deleted mid-upload
// (the expiry sweep running just after the first check) must not orphan the bytes
// just written: step three compensates by dropping them and reporting gone.
// RemoveKind, not Remove — the latter would delete the whole project directory
// over a race on one upload.
func (s *FileService) Store(ctx context.Context, put FilePut, body io.Reader) (protocol.FileWriteResponse, error) {
	if _, err := s.Projects.GetProject(ctx, put.ID); isMissing(err) {
		return protocol.FileWriteResponse{}, err
	}
	rel, err := s.Files.PutStream(put.ID, put.Kind, put.Ext, body)
	if err != nil {
		return protocol.FileWriteResponse{}, err
	}
	if put.Kind == protocol.KindOriginal || put.Kind == protocol.KindResult {
		rec, err := s.Projects.SetFile(ctx, put.ID, put.Kind, rel, put.W, put.H)
		switch {
		case isMissing(err):
			_ = s.Files.RemoveKind(put.ID, put.Kind)
			return protocol.FileWriteResponse{}, err
		case err != nil:
			return protocol.FileWriteResponse{}, fmt.Errorf("%w: %v", ErrRecordFile, err)
		}
		announce(ctx, s.Bus, protocol.EventUpdated, rec)
	} else if _, err := s.Projects.GetProject(ctx, put.ID); isMissing(err) {
		_ = s.Files.RemoveKind(put.ID, put.Kind)
		return protocol.FileWriteResponse{}, err
	}
	return protocol.FileWriteResponse{Path: rel, W: put.W, H: put.H}, nil
}
