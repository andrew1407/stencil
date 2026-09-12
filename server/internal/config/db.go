// Postgres pool sizing (DB_*; the timeout in seconds), split out so config.go
// stays the list of tunables.
package config

import "time"

func loadDB(get getter, cfg *Config) error {
	var err error
	if cfg.DBMaxConns, err = positiveInt(get, "DB_MAX_CONNS", 0, 0); err != nil {
		return err
	}
	if cfg.DBMinConns, err = positiveInt(get, "DB_MIN_CONNS", 0, 0); err != nil {
		return err
	}
	seconds, err := positiveInt(get, "DB_STATEMENT_TIMEOUT", 0, 0)
	if err != nil {
		return err
	}
	cfg.DBStatementTimeout = time.Duration(seconds) * time.Second
	return nil
}
