-- Add the per-project custom name accent colour to existing databases.
-- "#rrggbb" lower-case, or "" => use the client's current theme accent.
-- Idempotent: re-running at every boot is safe (the init DDL already declares
-- the column for fresh installs).
ALTER TABLE projects ADD COLUMN IF NOT EXISTS color text NOT NULL DEFAULT '';
