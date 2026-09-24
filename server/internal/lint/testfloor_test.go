package lint

import (
	"go/ast"
	"go/parser"
	"go/token"
	"go/types"
	"io/fs"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
	"unicode"
)

// A floor, not a pin: raise it when the suite grows a lot. Additions never trip it.
const testCountFloor = 235

// skipDirs are never scanned: vendor is third-party, data is runtime output.
var skipDirs = map[string]bool{"vendor": true, "data": true, "node_modules": true, ".git": true}

func here() string {
	_, file, _, _ := runtime.Caller(0)
	return filepath.Dir(file)
}

func repoRoot(t *testing.T) string {
	t.Helper()
	for dir := here(); ; {
		if _, err := os.Stat(filepath.Join(dir, ".git")); err == nil {
			return dir
		}
		parent := filepath.Dir(dir)
		if parent == dir {
			t.Fatalf("no repo root (.git) above %s", here())
		}
		dir = parent
	}
}

// A green run proves nothing if most of the suite never ran. Go exposes no test registry,
// so this counts declarations: it catches a lost test file, not one that stopped running.
func TestTestCountFloor(t *testing.T) {
	found := countTestFuncs(t, filepath.Join(repoRoot(t), "server"))
	if found < testCountFloor {
		t.Errorf("server suite collapsed to %d tests, floor is %d", found, testCountFloor)
	}
}

func countTestFuncs(t *testing.T, base string) int {
	t.Helper()
	fset, total := token.NewFileSet(), 0
	err := filepath.WalkDir(base, func(path string, d fs.DirEntry, err error) error {
		switch {
		case err != nil:
			return err
		case d.IsDir() && skipDirs[d.Name()]:
			return fs.SkipDir
		case d.IsDir(), !strings.HasSuffix(path, "_test.go"):
			return nil
		}
		file, err := parser.ParseFile(fset, path, nil, 0)
		if err != nil {
			return err
		}
		for _, decl := range file.Decls {
			if fn, ok := decl.(*ast.FuncDecl); ok && isTestFunc(fn) {
				total++
			}
		}
		return nil
	})
	if err != nil {
		t.Fatalf("walk %s: %v", base, err)
	}
	return total
}

// What `go test` itself runs: a top-level Test<Upper>(*testing.T). TestMain is the hook.
func isTestFunc(fn *ast.FuncDecl) bool {
	name := fn.Name.Name
	if fn.Recv != nil || name == "TestMain" || !strings.HasPrefix(name, "Test") || len(name) == 4 {
		return false
	}
	if unicode.IsLower([]rune(name[4:])[0]) {
		return false
	}
	params := fn.Type.Params.List
	return len(params) == 1 && types.ExprString(params[0].Type) == "*testing.T"
}
