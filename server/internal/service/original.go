package service

// The legacy inline original: "data:image/<type>[;…];base64,<payload>", what the browser's
// FileReader.readAsDataURL writes and all projects.original_content ever held. A create that still sends
// one, and the 0006 back-fill of the dropped column, both store it as the original file the upload writes.

import (
	"context"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"log"
	"strings"

	"stencil/server/internal/filestore"
	"stencil/server/internal/protocol"
	"stencil/server/internal/store"
)

// inlineOriginal is one parsed payload: ext is the media subtype, as the browser's own upload names it.
type inlineOriginal struct{ ext, payload string }

// parseInlineOriginal accepts only a base64 image data URL and checks the whole payload decodes, so a
// refusal lands before any row or byte is written.
func parseInlineOriginal(s string) (inlineOriginal, error) {
	if len(s) < 5 || !strings.EqualFold(s[:5], "data:") {
		return inlineOriginal{}, ErrBadOriginal
	}
	head, payload, ok := strings.Cut(s[5:], ",")
	params := strings.Split(head, ";")
	sub, image := strings.CutPrefix(strings.ToLower(strings.TrimSpace(params[0])), "image/")
	if !ok || !image || sub == "" || len(params) < 2 || !strings.EqualFold(params[len(params)-1], "base64") {
		return inlineOriginal{}, ErrBadOriginal
	}
	o := inlineOriginal{ext: sub, payload: payload}
	if _, err := io.Copy(io.Discard, o.bytes()); err != nil {
		return inlineOriginal{}, ErrBadOriginal
	}
	return o, nil
}

// bytes streams the decoded image; the base64 decoder skips the line breaks a wrapped payload carries.
func (o inlineOriginal) bytes() io.Reader {
	return base64.NewDecoder(base64.StdEncoding, strings.NewReader(o.payload))
}

// storeInline stores a create's inline original through the upload path, charged to its creator. A
// refusal takes the new row and any bytes back, so a create lands whole or not at all.
func (s *ProjectService) storeInline(ctx context.Context, rec protocol.ProjectRecord, o inlineOriginal, writer string) (protocol.ProjectRecord, error) {
	if s.Originals == nil {
		return protocol.ProjectRecord{}, fmt.Errorf("%w: no file service", ErrStoreOriginal)
	}
	put := FilePut{ID: rec.ID, Kind: protocol.KindOriginal, Ext: o.ext, W: rec.ImageW, H: rec.ImageH, Writer: writer}
	stored, _, err := s.Originals.put(ctx, put, o.bytes())
	if err == nil {
		return *stored, nil
	}
	// The create's context may be what failed, so the take-back runs on its own op timeout.
	dctx, cancel := withOpTimeout(context.WithoutCancel(ctx), s.Originals.OpTimeout)
	defer cancel()
	if derr := s.Projects.DeleteProject(dctx, rec.ID); derr != nil {
		log.Printf("service: take back project %s after its original was refused: %v", rec.ID, derr)
	}
	if s.Files != nil {
		if rerr := s.Files.Remove(rec.ID); rerr != nil { // the reconcile pass retries what is left behind
			log.Printf("service: drop bytes of refused project %s: %v", rec.ID, rerr)
		}
	}
	return protocol.ProjectRecord{}, fmt.Errorf("%w: %w", ErrStoreOriginal, err)
}

// LegacyOriginals is the 0006 back-fill's mover: each row's inline original becomes its original file,
// hashed as an upload is and charged to nobody. A payload that is no image answers store.ErrUndecodable.
func LegacyOriginals(files UploadFiles) store.MoveOriginal {
	return func(_ context.Context, row store.LegacyOriginal) (store.StoredFile, error) {
		o, err := parseInlineOriginal(row.Content)
		if err != nil {
			return store.StoredFile{}, fmt.Errorf("%w: %v", store.ErrUndecodable, err)
		}
		sum := sha256.New()
		rel, err := files.PutStreamAs(row.ID, protocol.KindOriginal, o.ext, io.TeeReader(o.bytes(), sum), filestore.Charge{})
		var unsafe filestore.ErrUnsafePath
		if errors.Is(err, filestore.ErrEmpty) || errors.As(err, &unsafe) {
			return store.StoredFile{}, fmt.Errorf("%w: %v", store.ErrUndecodable, err)
		}
		if err != nil {
			return store.StoredFile{}, err
		}
		return store.StoredFile{Kind: protocol.KindOriginal, Path: rel, Hash: hex.EncodeToString(sum.Sum(nil))}, nil
	}
}
