package store

// Cross-instance liveness (project_presence): each instance's live projects and member counts, so
// the expiry sweep and the delete guard on one instance see editors connected to another.

import (
	"context"
	"time"

	"github.com/jackc/pgx/v5"
)

// BeatPresence replaces instance's rows with live (project id → members), each trusted until
// now()+ttl on the database clock, and purges rows of other instances lapsed for a further ttl.
func (s *Store) BeatPresence(ctx context.Context, instance string, live map[string]int, ttl time.Duration) error {
	ids := make([]string, 0, len(live))
	members := make([]int32, 0, len(live))
	for id, n := range live {
		ids = append(ids, id)
		members = append(members, int32(n))
	}
	ttlMs := ttl.Milliseconds()
	return pgx.BeginFunc(ctx, s.pool, func(tx pgx.Tx) error {
		// Only rows lapsed a whole ttl ago are purged: their instance has stopped beating, so the purge
		// never contends with a live instance's own upsert.
		if _, err := tx.Exec(ctx, `DELETE FROM project_presence
			WHERE instance_id <> $1 AND live_until < now() - $2 * interval '1 millisecond'`, instance, ttlMs); err != nil {
			return err
		}
		if _, err := tx.Exec(ctx, `DELETE FROM project_presence
			WHERE instance_id = $1 AND project_id <> ALL($2)`, instance, ids); err != nil {
			return err
		}
		if len(ids) == 0 {
			return nil
		}
		_, err := tx.Exec(ctx, `INSERT INTO project_presence (project_id, instance_id, members, live_until)
			SELECT l.id, $1, l.n, now() + $4 * interval '1 millisecond'
			FROM unnest($2::text[], $3::int[]) AS l(id, n)
			ON CONFLICT (project_id, instance_id)
			DO UPDATE SET members = EXCLUDED.members, live_until = EXCLUDED.live_until`,
			instance, ids, members, ttlMs)
		return err
	})
}

// LiveElsewhere lists the projects another instance holds a live session on.
func (s *Store) LiveElsewhere(ctx context.Context, instance string) ([]string, error) {
	rows, err := s.pool.Query(ctx, `SELECT DISTINCT project_id FROM project_presence
		WHERE instance_id <> $1 AND live_until > now()`, instance)
	if err != nil {
		return nil, err
	}
	return pgx.CollectRows(rows, pgx.RowTo[string])
}

// MembersElsewhere sums a project's live editors on every other instance.
func (s *Store) MembersElsewhere(ctx context.Context, instance, projectID string) (int, error) {
	var n int
	err := s.pool.QueryRow(ctx, `SELECT COALESCE(SUM(members), 0)::int FROM project_presence
		WHERE project_id = $1 AND instance_id <> $2 AND live_until > now()`, projectID, instance).Scan(&n)
	return n, err
}
