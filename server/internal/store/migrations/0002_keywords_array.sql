-- Keywords move from a newline-joined text blob to a real text[] column.
-- The blob was a 1NF violation: it made keyword search impossible and hid the
-- dedupe/trim/order rule inside a serializer. Post-release columns must use
-- ADD COLUMN IF NOT EXISTS (see 0001_init.sql) — this file is re-run at every boot.

ALTER TABLE projects ADD COLUMN IF NOT EXISTS keywords_arr text[] NOT NULL DEFAULT '{}';

-- Back-fill from the legacy column. Idempotent: only rows that still carry the
-- blob and have no array yet are touched, so re-running is a no-op (and a row
-- whose keywords were cleared stays cleared — the writer clears both columns).
UPDATE projects
   SET keywords_arr = string_to_array(keywords, E'\n')
 WHERE keywords <> '' AND cardinality(keywords_arr) = 0;

-- Keyword search (keywords_arr @> ARRAY['x'], && for any-of).
CREATE INDEX IF NOT EXISTS projects_keywords_arr_idx ON projects USING GIN (keywords_arr);

-- The legacy `keywords` column stays for one release, still written but never
-- read, so a rollback to the previous server keeps its data. 0003 drops it.
