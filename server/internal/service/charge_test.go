package service

// STORAGE_QUOTA_PER_SESSION_BYTES at the service: an upload is charged to its writer, refused with the
// quota's own error, and with the cap off (or no writer) the ledger is never touched.

import (
	"context"
	"errors"
	"strings"
	"testing"

	"stencil/server/internal/filestore"
	"stencil/server/internal/protocol"
	"stencil/server/internal/store"
	"stencil/server/internal/testutil"
)

// noLedger fails any ledger call: the off path must not make one.
type noLedger struct{ t *testing.T }

func (l noLedger) Admit(context.Context, store.Charge, func(int64) error, func() error) error {
	l.t.Fatal("the ledger was asked to admit with the per-session cap off")
	return nil
}

func (l noLedger) Credit(context.Context, string, string) error {
	l.t.Fatal("the ledger was asked to credit with the per-session cap off")
	return nil
}

func chargedSvc(t *testing.T, quota int64) (*FileService, *testutil.MemStore) {
	t.Helper()
	svc, st, _ := fileSvc(t)
	svc.Charges, svc.SessionQuota = st, quota
	st.Seed(protocol.ProjectRecord{ID: "p_1"})
	st.Seed(protocol.ProjectRecord{ID: "p_2"})
	return svc, st
}

func upload(svc *FileService, id, kind, writer, body string) error {
	_, err := svc.Store(context.Background(), FilePut{ID: id, Kind: kind, Ext: "png", Writer: writer}, strings.NewReader(body))
	return err
}

func TestUploadsAreChargedToTheirWriter(t *testing.T) {
	svc, st := chargedSvc(t, 10)
	if err := upload(svc, "p_1", protocol.KindOriginal, "s_a", "123456"); err != nil {
		t.Fatal(err)
	}
	if err := upload(svc, "p_2", protocol.KindOriginal, "s_a", "12345"); !errors.Is(err, filestore.ErrQuotaExceeded) {
		t.Fatalf("past the writer's cap: %v, want the quota's own error", err)
	}
	if err := upload(svc, "p_2", protocol.KindOriginal, "s_b", "12345"); err != nil || st.SessionCharged("s_b") != 5 {
		t.Fatalf("another session: %v, charged %d", err, st.SessionCharged("s_b"))
	}
	// s_b replaces s_a's file: the charge moves, and s_a has room again.
	if err := upload(svc, "p_1", protocol.KindOriginal, "s_b", "1"); err != nil || st.SessionCharged("s_a") != 0 {
		t.Fatalf("replace: %v, s_a still charged %d", err, st.SessionCharged("s_a"))
	}
	if err := upload(svc, "p_2", "variant1", "s_a", "123456789"); err != nil {
		t.Fatalf("after the move: %v", err)
	}
	if err := svc.Remove(context.Background(), "p_2", "variant1"); err != nil || st.SessionCharged("s_a") != 0 {
		t.Fatalf("remove credits: %v, s_a %d", err, st.SessionCharged("s_a"))
	}
	if err := upload(svc, "p_2", protocol.KindResult, "", "123456789012"); err != nil {
		t.Fatalf("an upload with no session is charged to nobody: %v", err)
	}
}

func TestSessionQuotaOffNeverTouchesTheLedger(t *testing.T) {
	svc, _ := chargedSvc(t, 0)
	svc.Charges = noLedger{t}
	for i, kind := range []string{protocol.KindOriginal, protocol.KindResult, "variant2", protocol.KindChat} {
		if err := upload(svc, "p_1", kind, "s_a", strings.Repeat("x", 100*(i+1))); err != nil {
			t.Fatal(err)
		}
	}
	if err := svc.Remove(context.Background(), "p_1", "variant2"); err != nil {
		t.Fatal(err)
	}
}

// A failed ledger lookup refuses the upload rather than letting it through uncharged.
func TestLedgerFailureRefusesTheUpload(t *testing.T) {
	svc, _ := chargedSvc(t, 100)
	boom := errors.New("ledger down")
	svc.Charges = failingLedger{boom}
	if err := upload(svc, "p_1", protocol.KindOriginal, "s_a", "1"); !errors.Is(err, boom) {
		t.Fatalf("err = %v, want the lookup failure", err)
	}
}

type failingLedger struct{ err error }

func (l failingLedger) Admit(context.Context, store.Charge, func(int64) error, func() error) error {
	return l.err
}
func (l failingLedger) Credit(context.Context, string, string) error { return l.err }
