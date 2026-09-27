package main

import (
	"context"
	"fmt"
	"log"

	"stencil/server/internal/filestore"
	"stencil/server/internal/service"
	"stencil/server/internal/store"
)

// migrate brings the schema up to date, moving each legacy inline original into root unmetered (the store
// opened next counts it); with the per-session cap off it empties the ledger that cap left behind.
func migrate(ctx context.Context, st *store.Store, root string, sessionQuota int64) error {
	raw, err := filestore.New(root)
	if err != nil {
		return err
	}
	if err := store.MigrateWith(ctx, st.MigratePool(), service.LegacyOriginals(raw)); err != nil {
		return err
	}
	if sessionQuota > 0 {
		return nil
	}
	n, err := st.ClearCharges(ctx)
	if err != nil {
		return fmt.Errorf("clear the per-session storage ledger: %w", err)
	}
	if n > 0 {
		log.Printf("storage: STORAGE_QUOTA_PER_SESSION_BYTES is off — dropped %d per-session charge(s)", n)
	}
	return nil
}
