package filestore

// Opt-in: `go test -bench . ./internal/filestore/` (CI never passes -bench).
// Baselines are in server/README.md#benchmarks.

import (
	"bytes"
	"fmt"
	"testing"

	"stencil/server/internal/protocol"
)

// BenchmarkPut measures one atomic store: temp file, fsync, quota reserve, rename, directory sync. The
// fsync dominates, so compare runs on one machine only.
func BenchmarkPut(b *testing.B) {
	for _, size := range []int{4 << 10, 256 << 10, 4 << 20} {
		b.Run(fmt.Sprintf("size=%dKiB", size>>10), func(b *testing.B) {
			s, err := New(b.TempDir())
			if err != nil {
				b.Fatal(err)
			}
			data := bytes.Repeat([]byte{0x89}, size)
			b.SetBytes(int64(size))
			b.ReportAllocs()
			for i := 0; i < b.N; i++ {
				if _, err := s.Put(validID, protocol.KindOriginal, "png", data); err != nil {
					b.Fatal(err)
				}
			}
		})
	}
}

// BenchmarkPutStream is the same write from a reader — the upload path, which
// must not buffer the body a second time.
func BenchmarkPutStream(b *testing.B) {
	s, err := New(b.TempDir())
	if err != nil {
		b.Fatal(err)
	}
	data := bytes.Repeat([]byte{0x89}, 256<<10)
	b.SetBytes(int64(len(data)))
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		if _, err := s.PutStream(validID, protocol.KindResult, "png", bytes.NewReader(data)); err != nil {
			b.Fatal(err)
		}
	}
}
