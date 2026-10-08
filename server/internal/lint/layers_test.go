package lint

import (
	"go/parser"
	"go/token"
	"io/fs"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
)

// tiers is the server's layer order (ARCHITECTURE.md, Layers): a package may import only packages of a
// lower tier. The rigs and tools sit beside cmd at the top, so no product package can import them.
var tiers = map[string]int{
	"protocol": 0, "clock": 0, "ratelimit": 0, "transport": 0,
	"validate": 1, "auth": 1, "eventbus": 1, "llm": 1,
	"store": 2, "filestore": 2, "redisbus": 2,
	"service": 3, "hub": 3, "config": 3,
	"httpapi": 4,
	"cmd":     5, "testutil": 5, "lint": 5, "tools": 5,
}

const modulePrefix = "stencil/server/internal/"

// layerOf names the tier a server source file belongs to: its internal/<name> folder, or cmd.
func layerOf(rel string) string {
	parts := strings.Split(filepath.ToSlash(rel), "/")
	if parts[0] == "internal" && len(parts) > 2 {
		return parts[1]
	}
	return parts[0]
}

// Every internal import in non-test source points to a lower tier; the test files are free to reach the
// rigs. Parsed from the import blocks, so no toolchain call is needed.
func TestInternalImportsPointDownTheLayers(t *testing.T) {
	base := filepath.Join(repoRoot(t), "server")
	fset := token.NewFileSet()
	err := filepath.WalkDir(base, func(path string, d fs.DirEntry, err error) error {
		switch {
		case err != nil:
			return err
		case d.IsDir() && skipDirs[d.Name()]:
			return fs.SkipDir
		case d.IsDir(), !strings.HasSuffix(path, ".go"), strings.HasSuffix(path, "_test.go"):
			return nil
		}
		rel, _ := filepath.Rel(base, path)
		from := layerOf(rel)
		fromTier, ok := tiers[from]
		if !ok {
			t.Errorf("%s: package %q has no tier; add it to the layer order", rel, from)
			return nil
		}
		f, err := parser.ParseFile(fset, path, nil, parser.ImportsOnly)
		if err != nil {
			return err
		}
		for _, imp := range f.Imports {
			p, _ := strconv.Unquote(imp.Path.Value)
			if !strings.HasPrefix(p, modulePrefix) {
				continue
			}
			to := strings.SplitN(strings.TrimPrefix(p, modulePrefix), "/", 2)[0]
			if toTier, ok := tiers[to]; !ok || toTier >= fromTier {
				t.Errorf("%s: %s (tier %d) imports %s (tier %d); a layer uses only those before it", rel, from, fromTier, to, toTier)
			}
		}
		return nil
	})
	if err != nil {
		t.Fatal(err)
	}
}

func TestLayerOfNamesTheInternalFolderOrCmd(t *testing.T) {
	for rel, want := range map[string]string{
		"internal/hub/session.go":           "hub",
		"internal/tools/syncassets/main.go": "tools",
		"cmd/stencil-server/main.go":        "cmd",
	} {
		if got := layerOf(rel); got != want {
			t.Errorf("layerOf(%s) = %s, want %s", rel, got, want)
		}
	}
}
