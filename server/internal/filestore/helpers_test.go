package filestore

import (
	"bytes"
	"errors"
	"io"
	"os"
	"path/filepath"
)

// Test-only conveniences over the store's streaming API: the server itself never reads a file whole,
// never lists a project, and always writes from a reader.

func (s *Store) Put(id, kind, ext string, data []byte) (string, error) {
	return s.PutStreamAs(id, kind, ext, bytes.NewReader(data), Charge{})
}

func (s *Store) Get(id, kind, ext string) ([]byte, error) {
	full, err := s.safeJoin(id, kind, ext)
	if err != nil {
		return nil, err
	}
	data, err := os.ReadFile(full)
	if errors.Is(err, os.ErrNotExist) {
		return nil, ErrNotFound
	}
	return data, err
}

func (s *Store) GetByRelPath(rel string) ([]byte, error) {
	f, err := s.OpenByRelPath(rel)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	return io.ReadAll(f)
}

func (s *Store) List(id string) ([]string, error) {
	dir, err := s.projectDir(id)
	if err != nil {
		return nil, err
	}
	entries, err := os.ReadDir(dir)
	if errors.Is(err, os.ErrNotExist) {
		return nil, nil
	}
	out := make([]string, 0, len(entries))
	for _, e := range entries {
		if !e.IsDir() {
			rel, _ := filepath.Rel(s.root, filepath.Join(dir, e.Name()))
			out = append(out, filepath.ToSlash(rel))
		}
	}
	return out, err
}
