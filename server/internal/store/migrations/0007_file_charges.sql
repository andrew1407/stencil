-- STORAGE_QUOTA_PER_SESSION_BYTES: the session that wrote each stored file, and its bytes. One row per
-- (project, kind), moved to the next writer on a replacement; it goes with its project or its session.
CREATE TABLE IF NOT EXISTS file_charges (
    project_id text   NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
    kind       text   NOT NULL,
    session_id text   NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,
    bytes      bigint NOT NULL CHECK (bytes >= 0),
    PRIMARY KEY (project_id, kind)
);

-- One session's sum, read under that session's row lock on every charged upload.
CREATE INDEX IF NOT EXISTS file_charges_session_idx ON file_charges (session_id) INCLUDE (bytes);
