package service

// The upload saga. Storing one file spans two independent stores and cannot be
// wrapped in a transaction, so the compensating deletes live here with the
// steps they undo — not in whichever handler happens to call them.

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"hash"
	"io"
	"log"
	"time"

	"stencil/server/internal/eventbus"
	"stencil/server/internal/filestore"
	"stencil/server/internal/protocol"
	"stencil/server/internal/store"
)

// FileService owns writing a project file.
type FileService struct {
	Projects ProjectStore
	Files    UploadFiles
	Bus      eventbus.Bus
	Charges  ChargeLedger // charge.go; nil charges nobody
	// SessionQuota is STORAGE_QUOTA_PER_SESSION_BYTES, in bytes; 0 = off, and no upload touches Charges.
	SessionQuota int64
	// OpTimeout bounds each store call on its own (OP_TIMEOUT_SECONDS), so a slow body spends none of it.
	OpTimeout time.Duration
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
	Writer        string // the session the bytes are charged to; "" = nobody
}

// Store writes body as the project's bytes for one kind and announces a record it changed.
func (s *FileService) Store(ctx context.Context, put FilePut, body io.Reader) (protocol.FileWriteResponse, error) {
	rec, resp, err := s.put(ctx, put, body)
	if err != nil {
		return protocol.FileWriteResponse{}, err
	}
	if rec != nil {
		octx, cancel := withOpTimeout(ctx, s.OpTimeout)
		eventbus.PublishProjectEvent(octx, s.Bus, protocol.EventUpdated, *rec)
		cancel()
	}
	return resp, nil
}

// put is Store unannounced, answering the record it changed (nil for a filestore-only kind); a row deleted
// mid-upload is compensated by RemoveKind (not Remove) and reported gone (§9).
func (s *FileService) put(ctx context.Context, put FilePut, body io.Reader) (*protocol.ProjectRecord, protocol.FileWriteResponse, error) {
	var none protocol.FileWriteResponse
	if err := s.exists(ctx, put.ID); isMissing(err) {
		return nil, none, err
	}
	var sum hash.Hash
	if put.Kind == protocol.KindOriginal {
		sum = sha256.New()
		body = io.TeeReader(body, sum) // hashed on its way to disk: the bytes are read once
	}
	// The bytes count against the owner of the project they land in (STORAGE_QUOTA_PER_OWNER_BYTES).
	peers := func() ([]string, error) {
		octx, cancel := withOpTimeout(ctx, s.OpTimeout)
		defer cancel()
		return s.Projects.OwnerProjectIDs(octx, put.ID)
	}
	rel, err := s.Files.PutStreamAs(put.ID, put.Kind, put.Ext, body, filestore.Charge{Peers: peers, Admit: s.admit(ctx, put)})
	if err != nil {
		return nil, none, err
	}
	resp := protocol.FileWriteResponse{Path: rel, W: put.W, H: put.H}
	if put.Kind == protocol.KindOriginal || put.Kind == protocol.KindResult {
		file := store.StoredFile{Kind: put.Kind, Path: rel, W: put.W, H: put.H}
		if sum != nil {
			file.Hash = hex.EncodeToString(sum.Sum(nil))
			resp.OriginalHash = file.Hash
		}
		octx, cancel := withOpTimeout(ctx, s.OpTimeout)
		rec, err := s.Projects.SetFile(octx, put.ID, file)
		cancel()
		switch {
		case isMissing(err):
			s.compensate(put)
			return nil, none, err
		case err != nil:
			s.takeBack(ctx, put)
			return nil, none, fmt.Errorf("%w: %v", ErrRecordFile, err)
		}
		return &rec, resp, nil
	}
	if err := s.exists(ctx, put.ID); isMissing(err) {
		s.compensate(put)
		return nil, none, err
	}
	return nil, resp, nil
}

func (s *FileService) exists(ctx context.Context, id string) error {
	octx, cancel := withOpTimeout(ctx, s.OpTimeout)
	defer cancel()
	return exists(octx, s.Projects, id)
}

// takeBack removes, and uncharges, the bytes of an upload its row did not take, so the files route never
// serves bytes the record does not describe. It runs past a cancelled request on an op timeout of its own.
func (s *FileService) takeBack(ctx context.Context, put FilePut) {
	octx, cancel := withOpTimeout(context.WithoutCancel(ctx), s.OpTimeout)
	defer cancel()
	if err := s.Remove(octx, put.ID, put.Kind); err != nil {
		log.Printf("service: take back %s upload of project %s after its record failed: %v", put.Kind, put.ID, err)
	}
}

// compensate takes back the bytes of an upload whose project vanished; what it cannot remove is left
// for the filestore reconcile pass, which drops a directory no row owns.
func (s *FileService) compensate(put FilePut) {
	if err := s.Files.RemoveKind(put.ID, put.Kind); err != nil {
		log.Printf("service: compensate %s upload for vanished project %s: %v", put.Kind, put.ID, err)
	}
}
