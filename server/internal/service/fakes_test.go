package service

import (
	"io"
	"strings"
	"sync"

	"stencil/server/internal/filestore"
	"stencil/server/internal/store"
)

// listAll is the unpaged ListProjects argument the assertions use.
var listAll = store.ProjectPage{}

// fakeFiles records what the byte store was asked to do, and can fail a write.
type fakeFiles struct {
	mu          sync.Mutex
	removed     []string
	removedKind []string
	discarded   []string // staged uploads dropped instead of committed
	stored      map[string]string
	putErr      error
	peers       []string // what the last upload's peers lookup answered
}

func (f *fakeFiles) Remove(id string) error {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.removed = append(f.removed, id)
	return nil
}

func (f *fakeFiles) RemoveKind(id, kind string) error {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.removedKind = append(f.removedKind, id+"/"+kind)
	delete(f.stored, id+"/"+kind)
	return nil
}

// PutStreamAs is StageStreamAs committed at once, as the filestore's is.
func (f *fakeFiles) PutStreamAs(id, kind, ext string, r io.Reader, c filestore.Charge) (string, error) {
	st, err := f.StageStreamAs(id, kind, ext, r, c)
	if err != nil {
		return "", err
	}
	return st.Rel(), st.Commit()
}

func (f *fakeFiles) StageStreamAs(id, kind, ext string, r io.Reader, c filestore.Charge) (filestore.Staged, error) {
	if f.putErr != nil {
		return nil, f.putErr
	}
	if c.Peers != nil {
		ids, err := c.Peers()
		if err != nil {
			return nil, err
		}
		f.mu.Lock()
		f.peers = ids
		f.mu.Unlock()
	}
	body, err := io.ReadAll(r)
	if err != nil {
		return nil, err
	}
	st := &fakeStaged{f: f, key: id + "/" + kind, body: string(body), rel: strings.Join([]string{"projects", id, kind + "." + ext}, "/")}
	if c.Admit != nil {
		err = c.Admit(int64(len(body)), func() error { return nil })
	}
	if err != nil {
		return nil, err
	}
	return st, nil
}

// fakeStaged holds an upload's bytes until Commit stores them.
type fakeStaged struct {
	f              *fakeFiles
	key, body, rel string
}

func (s *fakeStaged) Rel() string { return s.rel }

func (s *fakeStaged) Commit() error {
	s.f.mu.Lock()
	defer s.f.mu.Unlock()
	if s.f.stored == nil {
		s.f.stored = map[string]string{}
	}
	s.f.stored[s.key] = s.body
	return nil
}

func (s *fakeStaged) Discard() error {
	s.f.mu.Lock()
	defer s.f.mu.Unlock()
	s.f.discarded = append(s.f.discarded, s.key)
	return nil
}

func (f *fakeFiles) kinds() []string {
	f.mu.Lock()
	defer f.mu.Unlock()
	out := make([]string, 0, len(f.stored))
	for k := range f.stored {
		out = append(out, k)
	}
	return out
}
