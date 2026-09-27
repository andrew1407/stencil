-- Cross-instance liveness: each instance's live projects and their member counts, trusted until
-- live_until on the database clock. No foreign key, so a heartbeat never waits on a project delete;
-- a row for a deleted project lapses with the rest.
CREATE TABLE IF NOT EXISTS project_presence (
    project_id  text        NOT NULL,
    instance_id text        NOT NULL,
    members     integer     NOT NULL CHECK (members >= 0),
    live_until  timestamptz NOT NULL,
    PRIMARY KEY (project_id, instance_id)
);
