-- Every upload under the per-owner quota lists the projects its owner session holds; an index on
-- owner_session serves that as a lookup instead of a scan of every project.
CREATE INDEX IF NOT EXISTS projects_owner_session_idx ON projects (owner_session) WHERE owner_session IS NOT NULL;
