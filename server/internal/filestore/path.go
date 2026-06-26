package filestore

import (
	"fmt"
	"path/filepath"
	"regexp"
	"strings"

	"stencil/server/internal/protocol"
)

// projectIDPattern is the only id shape the file store will touch. It matches
// the server-allocated project id format ("p_" + base36 + "_" + base36 salt).
// No user-supplied filename ever reaches the filesystem: the directory is keyed
// by a validated id and the filename is derived from a fixed kind allowlist plus
// a sanitized extension.
var projectIDPattern = regexp.MustCompile(`^p_[0-9a-z]+_[0-9a-z]+$`)

// extPattern restricts file extensions to a short alphanumeric token.
var extPattern = regexp.MustCompile(`^[0-9a-z]{1,8}$`)

// ErrUnsafePath is returned when an id/kind/ext fails validation or the resolved
// path would escape the store root.
type ErrUnsafePath struct{ Reason string }

func (e ErrUnsafePath) Error() string { return "filestore: unsafe path: " + e.Reason }

// validKind reports whether kind is an allowed file role.
func validKind(kind string) bool {
	return kind == protocol.KindOriginal || kind == protocol.KindResult
}

// normalizeExt lowercases and validates a file extension (no dot). An empty ext
// defaults to "bin".
func normalizeExt(ext string) (string, error) {
	ext = strings.TrimPrefix(strings.ToLower(strings.TrimSpace(ext)), ".")
	if ext == "" {
		ext = "bin"
	}
	if !extPattern.MatchString(ext) {
		return "", ErrUnsafePath{Reason: "extension " + ext}
	}
	return ext, nil
}

// projectDir returns the absolute, root-confined directory for a project id.
func (s *Store) projectDir(id string) (string, error) {
	if !projectIDPattern.MatchString(id) {
		return "", ErrUnsafePath{Reason: "project id " + id}
	}
	return s.confine(filepath.Join("projects", id))
}

// safeJoin resolves <root>/projects/<id>/<kind>.<ext>, validating every segment
// and re-checking that the cleaned absolute path is still inside the store root.
// This is the single choke point guarding against path traversal.
func (s *Store) safeJoin(id, kind, ext string) (string, error) {
	if !validKind(kind) {
		return "", ErrUnsafePath{Reason: "kind " + kind}
	}
	dir, err := s.projectDir(id)
	if err != nil {
		return "", err
	}
	cleanExt, err := normalizeExt(ext)
	if err != nil {
		return "", err
	}
	return s.confine(filepath.Join(dir, kind+"."+cleanExt))
}

// confine cleans p (which may be relative to root or absolute) and verifies the
// result lies within the store root, defeating "..", absolute paths and stray
// separators. Symlink escape is additionally checked at write/read time.
func (s *Store) confine(p string) (string, error) {
	var abs string
	if filepath.IsAbs(p) {
		abs = filepath.Clean(p)
	} else {
		abs = filepath.Clean(filepath.Join(s.root, p))
	}
	rootWithSep := s.root + string(filepath.Separator)
	if abs != s.root && !strings.HasPrefix(abs, rootWithSep) {
		return "", ErrUnsafePath{Reason: fmt.Sprintf("%q escapes root", p)}
	}
	return abs, nil
}
