// Package validate holds the server's pure request predicates: no I/O, no
// ResponseWriter, no store. Every caller that needs a rule asks here, so a
// bound is written once instead of being re-implemented per handler.
package validate

import (
	"errors"
	"regexp"
	"strconv"
)

// MaxListLimit caps ?limit= on GET /projects; a bigger page is refused rather
// than silently cut.
const MaxListLimit = 500

// ListLimit parses ?limit=. An empty value means "no limit" (0).
func ListLimit(raw string) (int, error) {
	if raw == "" {
		return 0, nil
	}
	n, err := strconv.Atoi(raw)
	if err != nil || n < 1 || n > MaxListLimit {
		return 0, errors.New("limit must be 1.." + strconv.Itoa(MaxListLimit))
	}
	return n, nil
}

// projectIDPattern is the server-allocated id shape, "p_" + base36(ms) + "_" + base36(salt): the only
// one the file store touches and the hub admits, so no client-supplied name reaches either.
var projectIDPattern = regexp.MustCompile(`^p_[0-9a-z]+_[0-9a-z]+$`)

// ProjectID reports whether id has the shape the server allocates.
func ProjectID(id string) bool { return projectIDPattern.MatchString(id) }
