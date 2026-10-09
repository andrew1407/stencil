-- No instance publishes live projects any more: a connection listens to the feed and edits over REST.
DROP TABLE IF EXISTS project_presence;
