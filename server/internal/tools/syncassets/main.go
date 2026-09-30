// Command syncassets copies one canonical shared table into this module, for `go generate`:
// go:embed cannot reach past the module root, so each embedded asset is a generated,
// gitignored copy of its common/config original, pinned by a drift test beside its embed.
package main

import (
	"bytes"
	"fmt"
	"os"
	"path/filepath"
)

func main() {
	if len(os.Args) != 3 {
		fmt.Fprintln(os.Stderr, "usage: syncassets <canonical source> <embedded copy>")
		os.Exit(2)
	}
	if err := sync(os.Args[1], os.Args[2]); err != nil {
		fmt.Fprintln(os.Stderr, "syncassets:", err)
		os.Exit(1)
	}
}

// sync writes dst only when its bytes differ, so an up-to-date copy keeps its mtime.
func sync(src, dst string) error {
	raw, err := os.ReadFile(src)
	if err != nil {
		return err
	}
	if cur, err := os.ReadFile(dst); err == nil && bytes.Equal(cur, raw) {
		return nil
	}
	if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
		return err
	}
	return os.WriteFile(dst, raw, 0o644)
}
