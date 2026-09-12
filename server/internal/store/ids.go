package store

import (
	"crypto/rand"
	"encoding/binary"
	"strconv"
)

// newID builds a server-allocated id of the form
// prefix + base36(nowMs) + "_" + base36(random-salt). The base36 encoding mirrors
// the JS Number.toString(36) convention used by core's ProjectsStore, and the
// shape is exactly what the filestore's id allowlist accepts.
func newID(prefix string, nowMs int64) (string, error) {
	var saltBytes [6]byte
	if _, err := rand.Read(saltBytes[:]); err != nil {
		return "", err
	}
	salt := binary.BigEndian.Uint64(append([]byte{0, 0}, saltBytes[:]...))
	return prefix + strconv.FormatInt(nowMs, 36) + "_" + strconv.FormatUint(salt, 36), nil
}

// newProjectID and newSessionID are the two id spaces; the prefix is the only
// difference and it is what the filestore's allowlist keys off.
func newProjectID(nowMs int64) (string, error) { return newID("p_", nowMs) }
func newSessionID(nowMs int64) (string, error) { return newID("s_", nowMs) }
