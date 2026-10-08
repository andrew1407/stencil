package service

// STORAGE_QUOTA_PER_SESSION_BYTES: a cap on the bytes one session has written, whoever owns the projects
// they landed in. The ledger is kept only while the cap is on; off, no upload or removal touches it.

import (
	"context"

	"stencil/server/internal/filestore"
	"stencil/server/internal/store"
)

// charging reports whether writes are charged at all.
func (s *FileService) charging() bool { return s.SessionQuota > 0 && s.Charges != nil }

// admit is the upload's ledger step, nil when the cap is off or no session wrote the bytes. A refusal is
// the aggregate quota's own error, so every quota answers alike; a failed lookup refuses too.
func (s *FileService) admit(ctx context.Context, put FilePut) func(n int64, commit func() error) error {
	if !s.charging() || put.Writer == "" {
		return nil
	}
	return func(n int64, commit func() error) error {
		c := store.Charge{ProjectID: put.ID, Kind: put.Kind, SessionID: put.Writer, Bytes: n}
		fits := func(held int64) error {
			if held+n > s.SessionQuota {
				return filestore.ErrQuotaExceeded
			}
			return nil
		}
		octx, cancel := withOpTimeout(ctx, s.OpTimeout)
		defer cancel()
		return s.Charges.Admit(octx, c, fits, commit)
	}
}

// Remove deletes one kind's bytes, crediting their writer first: a failure between the two leaves bytes
// charged to nobody, never a charge for bytes that are gone. Removing an absent kind is not an error.
func (s *FileService) Remove(ctx context.Context, id, kind string) error {
	if s.charging() {
		if err := s.Charges.Credit(ctx, id, kind); err != nil {
			return err
		}
	}
	return s.Files.RemoveKind(id, kind)
}
