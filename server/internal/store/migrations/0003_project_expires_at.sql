-- Add the per-project expiration timestamp to existing databases.
-- Epoch ms; 0 (the default) means "keep forever". A project with a non-zero
-- expires_at at or before now() is removed by the server's startup + periodic
-- expiry sweep. Idempotent: re-running at every boot is safe (the init DDL
-- already declares the column for fresh installs).
ALTER TABLE projects ADD COLUMN IF NOT EXISTS expires_at bigint NOT NULL DEFAULT 0;

-- Speeds up the sweep's "expires_at > 0 AND expires_at <= now" scan.
CREATE INDEX IF NOT EXISTS projects_expires_at_idx ON projects (expires_at) WHERE expires_at > 0;
