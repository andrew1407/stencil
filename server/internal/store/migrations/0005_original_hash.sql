-- The SHA-256 of the stored original (lowercase hex), recorded as the upload streams to disk, so a
-- client can tell a replaced original from a layout edit. NULL = unknown: no original yet, or one
-- stored before this column; rows are not back-filled, and a client reads NULL as "reload".
ALTER TABLE projects ADD COLUMN IF NOT EXISTS original_hash text;
