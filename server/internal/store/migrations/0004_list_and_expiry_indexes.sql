-- GET /projects walks a keyset over (updated_at DESC, id DESC); an index in exactly that order serves
-- every page as a range scan, and supersedes the single-column one from 0001.
CREATE INDEX IF NOT EXISTS projects_updated_id_idx ON projects (updated_at DESC, id DESC);
DROP INDEX IF EXISTS projects_updated_at_idx;

-- The expiry sweep deletes sessions past their expires_at (0 = never), as it does projects.
CREATE INDEX IF NOT EXISTS sessions_expires_at_idx ON sessions (expires_at) WHERE expires_at > 0;
