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
    has_image        boolean NOT NULL DEFAULT false,
    image_w          integer NOT NULL DEFAULT 0,
    image_h          integer NOT NULL DEFAULT 0,
    source           text    NOT NULL DEFAULT '',   -- media URL (provenance)
    resource         text    NOT NULL DEFAULT '',   -- origin web page (provenance)
    color            text    NOT NULL DEFAULT '',   -- custom name accent "#rrggbb" or "" (theme default)
    original_path    text    NOT NULL DEFAULT '',   -- filestore-relative original
    result_path      text    NOT NULL DEFAULT '',   -- filestore-relative rendered result
    original_content text    NOT NULL DEFAULT '',   -- original payload kept for re-fetch
    layout           jsonb,                          -- JSON layout payload
    owner_session    text    REFERENCES sessions(id) ON DELETE SET NULL,
    version          integer NOT NULL DEFAULT 0      -- monotonic edit version (LWW guard)
);

CREATE INDEX IF NOT EXISTS projects_updated_at_idx ON projects (updated_at DESC);
