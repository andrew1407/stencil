-- The promised end of the keywords move (0002): every reader uses keywords_arr, and the server
-- stopped writing the newline-joined blob, so the legacy column goes.

ALTER TABLE projects DROP COLUMN IF EXISTS keywords;
