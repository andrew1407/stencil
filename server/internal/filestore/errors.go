package filestore

import "errors"

var (
	// ErrNotFound is returned when a requested file does not exist.
	ErrNotFound = errors.New("filestore: not found")
	// ErrQuotaExceeded is returned by Put when a write would push the aggregate
	// stored bytes past the configured quota.
	ErrQuotaExceeded = errors.New("filestore: storage quota exceeded")
	// ErrEmpty is returned when a write carries no bytes at all.
	ErrEmpty = errors.New("filestore: empty file")
)
