-- Initial schema for the Stencil collaboration server.
-- sessions: one row per issued bearer token (only its sha256 hash is stored).
-- projects: shared project metadata + payload; image bytes live in the filestore.

CREATE TABLE IF NOT EXISTS sessions (
    id          text   PRIMARY KEY,
    token_hash  bytea  NOT NULL UNIQUE,
    label       text   NOT NULL DEFAULT '',
    created_at  bigint NOT NULL,   -- epoch ms
    expires_at  bigint NOT NULL    -- epoch ms (0 = never)
);

CREATE TABLE IF NOT EXISTS projects (
    id               text    PRIMARY KEY,        -- "p_" + base36(now) + "_" + base36(salt)
    name             text    NOT NULL,
    created_at       bigint  NOT NULL,           -- epoch ms
    updated_at       bigint  NOT NULL,           -- epoch ms (lists sort desc)
    expires_at       bigint  NOT NULL DEFAULT 0, -- epoch ms (0 = never; swept once past)
    has_image        boolean NOT NULL DEFAULT false,
    image_w          integer NOT NULL DEFAULT 0,
    image_h          integer NOT NULL DEFAULT 0,
    source           text    NOT NULL DEFAULT '',   -- media URL (provenance)
    resource         text    NOT NULL DEFAULT '',   -- origin web page (provenance)
    color            text    NOT NULL DEFAULT '',   -- custom name accent "#rrggbb" or "" (theme default)
    description      text    NOT NULL DEFAULT '',   -- free-text project description ("" = none)
    keywords         text    NOT NULL DEFAULT '',   -- newline-joined search keywords ("" = none)
    blank_color      text    NOT NULL DEFAULT '',   -- blank-image fill "#rrggbb" ("" = not a blank)
    original_path    text    NOT NULL DEFAULT '',   -- filestore-relative original
    result_path      text    NOT NULL DEFAULT '',   -- filestore-relative rendered result
    original_content text    NOT NULL DEFAULT '',   -- original payload kept for re-fetch
    layout           jsonb,                          -- JSON layout payload
    owner_session    text    REFERENCES sessions(id) ON DELETE SET NULL,
    version          integer NOT NULL DEFAULT 0      -- monotonic edit version (LWW guard)
);

-- Idempotent column back-fill: CREATE TABLE IF NOT EXISTS is a no-op on an already-provisioned
-- database, so every column added to projects AFTER its first release must also be ADDed here or
-- an existing DB never gains it (and INSERT/scan then fails at runtime). ADD COLUMN IF NOT EXISTS
-- is a no-op on a fresh DB (the CREATE above just made the column) and back-fills the DEFAULT on
-- an old one, so this stays safe to re-run every boot — matching the version-table-free model.
ALTER TABLE projects ADD COLUMN IF NOT EXISTS expires_at       bigint  NOT NULL DEFAULT 0;
ALTER TABLE projects ADD COLUMN IF NOT EXISTS has_image        boolean NOT NULL DEFAULT false;
ALTER TABLE projects ADD COLUMN IF NOT EXISTS image_w          integer NOT NULL DEFAULT 0;
ALTER TABLE projects ADD COLUMN IF NOT EXISTS image_h          integer NOT NULL DEFAULT 0;
ALTER TABLE projects ADD COLUMN IF NOT EXISTS source           text    NOT NULL DEFAULT '';
ALTER TABLE projects ADD COLUMN IF NOT EXISTS resource         text    NOT NULL DEFAULT '';
ALTER TABLE projects ADD COLUMN IF NOT EXISTS color            text    NOT NULL DEFAULT '';
ALTER TABLE projects ADD COLUMN IF NOT EXISTS description      text    NOT NULL DEFAULT '';
ALTER TABLE projects ADD COLUMN IF NOT EXISTS keywords         text    NOT NULL DEFAULT '';
ALTER TABLE projects ADD COLUMN IF NOT EXISTS blank_color      text    NOT NULL DEFAULT '';
ALTER TABLE projects ADD COLUMN IF NOT EXISTS original_path    text    NOT NULL DEFAULT '';
ALTER TABLE projects ADD COLUMN IF NOT EXISTS result_path      text    NOT NULL DEFAULT '';
ALTER TABLE projects ADD COLUMN IF NOT EXISTS original_content text    NOT NULL DEFAULT '';
ALTER TABLE projects ADD COLUMN IF NOT EXISTS layout           jsonb;
ALTER TABLE projects ADD COLUMN IF NOT EXISTS owner_session    text    REFERENCES sessions(id) ON DELETE SET NULL;
ALTER TABLE projects ADD COLUMN IF NOT EXISTS version          integer NOT NULL DEFAULT 0;

CREATE INDEX IF NOT EXISTS projects_updated_at_idx ON projects (updated_at DESC);

-- Speeds up the expiry sweep's "expires_at > 0 AND expires_at <= now" scan.
CREATE INDEX IF NOT EXISTS projects_expires_at_idx ON projects (expires_at) WHERE expires_at > 0;
