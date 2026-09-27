-- The end of the inline original (0001's original_content): the Go step 0006_original_content_backfill,
-- applied just before this file in the same transaction, moved each one to the filestore. The guard
-- refuses to drop a column that still holds an original no file replaced; nested, since the inner
-- query is planned only once the column is known to exist.
DO $$
BEGIN
    IF EXISTS (SELECT 1 FROM information_schema.columns
               WHERE table_schema = current_schema() AND table_name = 'projects'
                 AND column_name = 'original_content') THEN
        IF EXISTS (SELECT 1 FROM projects WHERE octet_length(original_content) > 0 AND original_path = '') THEN
            RAISE EXCEPTION 'original_content still holds an original the back-fill did not move';
        END IF;
    END IF;
END $$;

ALTER TABLE projects DROP COLUMN IF EXISTS original_content;
