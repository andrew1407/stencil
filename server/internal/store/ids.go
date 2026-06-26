package store

import (
	"crypto/rand"
	"encoding/binary"
	"strconv"
)

// newProjectID builds a server-allocated project id of the form
// "p_" + base36(nowMs) + "_" + base36(random-salt). The base36 encoding mirrors
// the JS Number.toString(36) convention used by core's ProjectsStore, and the
// shape is exactly what the filestore's id allowlist accepts.
func newProjectID(nowMs int64) (string, error) {
	var saltBytes [6]byte
	if _, err := rand.Read(saltBytes[:]); err != nil {
		return "", err
	}
	salt := binary.BigEndian.Uint64(append([]byte{0, 0}, saltBytes[:]...))
	return "p_" + strconv.FormatInt(nowMs, 36) + "_" + strconv.FormatUint(salt, 36), nil
}

// newSessionID builds a session id "s_" + base36 time + random salt.
func newSessionID(nowMs int64) (string, error) {
	var saltBytes [6]byte
	if _, err := rand.Read(saltBytes[:]); err != nil {
		return "", err
	}
	salt := binary.BigEndian.Uint64(append([]byte{0, 0}, saltBytes[:]...))
	return "s_" + strconv.FormatInt(nowMs, 36) + "_" + strconv.FormatUint(salt, 36), nil
}
