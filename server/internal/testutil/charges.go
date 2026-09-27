package testutil

// The ledger half of MemStore: service.ChargeLedger, one charge per (project, kind) as file_charges
// keeps it, dropped with its project as the real foreign key cascades.

import (
	"context"

	"stencil/server/internal/store"
)

type chargeKey struct{ project, kind string }

type memCharge struct {
	session string
	bytes   int64
}

// Admit mirrors the store: fits sees the session's other files, and commit runs before the charge lands.
// It holds the store's lock throughout, standing in for the session row lock.
func (f *MemStore) Admit(_ context.Context, c store.Charge, fits func(held int64) error, commit func() error) error {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.ledgerCalls++
	if _, ok := f.projects[c.ProjectID]; !ok {
		return store.ErrNotFound
	}
	key := chargeKey{project: c.ProjectID, kind: c.Kind}
	var held int64
	for k, ch := range f.charges {
		if ch.session == c.SessionID && k != key {
			held += ch.bytes
		}
	}
	if err := fits(held); err != nil {
		return err
	}
	if err := commit(); err != nil {
		return err
	}
	f.charges[key] = memCharge{session: c.SessionID, bytes: c.Bytes}
	return nil
}

func (f *MemStore) Credit(_ context.Context, projectID, kind string) error {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.ledgerCalls++
	delete(f.charges, chargeKey{project: projectID, kind: kind})
	return nil
}

// SessionCharged sums what session holds, as the store's query does.
func (f *MemStore) SessionCharged(session string) int64 {
	f.mu.Lock()
	defer f.mu.Unlock()
	var n int64
	for _, ch := range f.charges {
		if ch.session == session {
			n += ch.bytes
		}
	}
	return n
}

// LedgerCalls reports how many Admit and Credit calls reached the ledger.
func (f *MemStore) LedgerCalls() int {
	f.mu.Lock()
	defer f.mu.Unlock()
	return f.ledgerCalls
}

// dropCharges is the foreign key's cascade. Callers hold f.mu.
func (f *MemStore) dropCharges(project string) {
	for k := range f.charges {
		if k.project == project {
			delete(f.charges, k)
		}
	}
}
