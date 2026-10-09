// Package service holds the server's application policy: the rules that must hold however a project is
// reached, and the multi-store sequences no transport should re-implement. The REST handlers and the
// expiry sweep both drive these.
//
// It owns no transport concepts: failures come back as the sentinels below (plus the store's ErrNotFound).
package service

import (
	"context"
	"errors"

	"stencil/server/internal/clock"
	"stencil/server/internal/store"
)

// Policy failures. Everything else is the underlying store's error, passed
// through so a caller can still tell a missing project from a broken one.
var (
	// ErrImageRequired: a Stencil project is created FROM an image.
	ErrImageRequired = errors.New("service: a project must be created from an image")
	// ErrRecordFile: the bytes landed but the project row did not take them.
	ErrRecordFile = errors.New("service: could not record the stored file")
	// ErrBadOriginal: a create's inline original is no base64 image data URL.
	ErrBadOriginal = errors.New("service: originalContent is not a base64 image data URL")
	// ErrStoreOriginal: a create's inline original was refused; it wraps the upload path's own error.
	ErrStoreOriginal = errors.New("service: could not store the original")
)

// exists turns a missing row into store.ErrNotFound, the one failure a caller branches on.
func exists(ctx context.Context, projects ProjectStore, id string) error {
	ok, err := projects.ProjectExists(ctx, id)
	if err == nil && !ok {
		err = store.ErrNotFound
	}
	return err
}

// isMissing reports whether err says the project is gone.
func isMissing(err error) bool { return errors.Is(err, store.ErrNotFound) }

// withOpTimeout bounds one store call; a zero timeout leaves ctx as it is.
var withOpTimeout = clock.WithTimeout
