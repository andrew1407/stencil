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

// fakeCounter stands in for the hub's live connection count.
type fakeCounter int

func (f fakeCounter) ConnectionCount(string) int { return int(f) }

// fakeFiles records what the byte store was asked to do, and can fail a write.
type fakeFiles struct {
	mu          sync.Mutex
	removed     []string
	removedKind []string
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

func (f *fakeFiles) PutStreamAs(id, kind, ext string, r io.Reader, c filestore.Charge) (string, error) {
	if f.putErr != nil {
		return "", f.putErr
	}
	if c.Peers != nil {
		ids, err := c.Peers()
		if err != nil {
			return "", err
		}
		f.mu.Lock()
		f.peers = ids
		f.mu.Unlock()
	}
	body, err := io.ReadAll(r)
	if err != nil {
		return "", err
	}
	commit := func() error {
		f.mu.Lock()
		defer f.mu.Unlock()
		if f.stored == nil {
			f.stored = map[string]string{}
		}
		f.stored[id+"/"+kind] = string(body)
		return nil
	}
	if c.Admit != nil {
		err = c.Admit(int64(len(body)), commit)
	} else {
		err = commit()
	}
	if err != nil {
		return "", err
	}
	return strings.Join([]string{"projects", id, kind + "." + ext}, "/"), nil
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
