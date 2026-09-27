package store

// The 0006 back-fill: an original that lives only in the legacy projects.original_content column (the
// inline data URL POST /projects once kept) moves to the filestore before 0006's SQL drops the column,
// in the same migration transaction, so a crash between the two re-runs both.

import (
	"context"
	"errors"
	"fmt"
	"log"

	"github.com/jackc/pgx/v5"
)

// goStep is a migration that must run Go, not SQL; it is ordered and recorded with the files by name.
type goStep func(ctx context.Context, tx pgx.Tx, move MoveOriginal) error

// originalBackfill sorts just before 0006_original_content_drop.sql.
const originalBackfill = "0006_original_content_backfill"

var goSteps = map[string]goStep{originalBackfill: backfillOriginals}

// ErrUndecodable marks a legacy original that is no image payload: logged, and dropped with the column.
var ErrUndecodable = errors.New("store: legacy original is not an image payload")

// LegacyOriginal is one row whose original is only its inline content.
type LegacyOriginal struct{ ID, Content string }

// MoveOriginal stores one legacy original's bytes and answers the file its row now records (Path, Hash).
type MoveOriginal func(ctx context.Context, row LegacyOriginal) (StoredFile, error)

// backfillOriginals moves every row with content but no uploaded original; one that has an upload keeps
// it. Rows are read one at a time, since each content can be a whole MAX_BODY_BYTES image.
func backfillOriginals(ctx context.Context, tx pgx.Tx, move MoveOriginal) error {
	var present bool
	if err := tx.QueryRow(ctx, `SELECT EXISTS (SELECT 1 FROM information_schema.columns
		WHERE table_schema = current_schema() AND table_name = 'projects' AND column_name = 'original_content')`).
		Scan(&present); err != nil || !present {
		return err
	}
	rows, err := tx.Query(ctx, `SELECT id FROM projects
		WHERE octet_length(original_content) > 0 AND original_path = '' ORDER BY id`)
	if err != nil {
		return err
	}
	ids, err := pgx.CollectRows(rows, pgx.RowTo[string])
	if err != nil {
		return err
	}
	if len(ids) > 0 && move == nil {
		return fmt.Errorf("%d project(s) hold their original only in original_content; boot the server to move them", len(ids))
	}
	moved := 0
	for _, id := range ids {
		ok, err := backfillOne(ctx, tx, id, move)
		if err != nil {
			return fmt.Errorf("project %s: %w", id, err)
		}
		if ok {
			moved++
		}
	}
	if len(ids) > 0 {
		log.Printf("migrate: moved %d inline original(s) to the filestore, dropped %d undecodable", moved, len(ids)-moved)
	}
	return nil
}

// backfillOne moves one row's original, or clears content that is no image so the drop's guard passes;
// it reports whether the original moved. Version and updated_at stay: the picture did not change.
func backfillOne(ctx context.Context, tx pgx.Tx, id string, move MoveOriginal) (bool, error) {
	var content string
	if err := tx.QueryRow(ctx, `SELECT original_content FROM projects WHERE id = $1`, id).Scan(&content); err != nil {
		return false, err
	}
	f, err := move(ctx, LegacyOriginal{ID: id, Content: content})
	if errors.Is(err, ErrUndecodable) {
		log.Printf("migrate: project %s: dropping a %d-byte inline original: %v", id, len(content), err)
		_, err = tx.Exec(ctx, `UPDATE projects SET original_content = '' WHERE id = $1`, id)
		return false, err
	}
	if err != nil {
		return false, err
	}
	_, err = tx.Exec(ctx, `UPDATE projects SET original_path = $2, original_hash = NULLIF($3, ''), has_image = true
		WHERE id = $1`, id, f.Path, f.Hash)
	return err == nil, err
}
