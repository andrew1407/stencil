package lint

import (
	"encoding/json"
	"io/fs"
	"os"
	"path/filepath"
	"runtime"
	"sort"
	"strings"
	"testing"
)

// budget is the committed ratchet in sizebudget.json.
type budget struct {
	MaxNewFileLines int               `json:"maxNewFileLines"`
	Files           map[string]int    `json:"files"`
	CommentPct      map[string]int    `json:"commentPct"`
	Exceptions      map[string]string `json:"exceptions"`
}

type counts struct{ Total, Comment int }

// skipDirs are never scanned: vendor is third-party, data is runtime output.
var skipDirs = map[string]bool{"vendor": true, "data": true, "node_modules": true, ".git": true}

func here() string {
	_, file, _, _ := runtime.Caller(0)
	return filepath.Dir(file)
}

// load reads the budget and scans the tree it governs. Paths are relative to the
// repo root (nearest .git above this file), so they read the same anywhere.
func load(t *testing.T) (budget, map[string]counts) {
	t.Helper()
	raw, err := os.ReadFile(filepath.Join(here(), "sizebudget.json"))
	if err != nil {
		t.Fatalf("read budget: %v", err)
	}
	var b budget
	if err := json.Unmarshal(raw, &b); err != nil {
		t.Fatalf("parse budget: %v", err)
	}
	if b.MaxNewFileLines <= 0 {
		t.Fatal("budget: maxNewFileLines must be positive")
	}
	return b, scanTree(t, repoRoot(t))
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

// scanTree returns every in-scope .go file under server/, keyed by repo-relative path.
func scanTree(t *testing.T, root string) map[string]counts {
	t.Helper()
	out := map[string]counts{}
	base := filepath.Join(root, "server")
	err := filepath.WalkDir(base, func(path string, d fs.DirEntry, err error) error {
		switch {
		case err != nil:
			return err
		case d.IsDir() && skipDirs[d.Name()]:
			return fs.SkipDir
		case d.IsDir(), filepath.Ext(path) != ".go":
			return nil
		}
		rel, err := filepath.Rel(root, path)
		if err != nil {
			return err
		}
		out[filepath.ToSlash(rel)] = countFile(t, path)
		return nil
	})
	if err != nil {
		t.Fatalf("walk %s: %v", base, err)
	}
	if len(out) == 0 {
		t.Fatalf("no .go files found under %s", base)
	}
	return out
}

func countFile(t *testing.T, path string) counts {
	t.Helper()
	raw, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read %s: %v", path, err)
	}
	text := strings.TrimSuffix(string(raw), "\n")
	if text == "" {
		return counts{}
	}
	c, st := counts{}, scanState{}
	for _, line := range strings.Split(text, "\n") {
		var isComment bool
		isComment, st = scanLine(line, st)
		c.Total++
		if isComment {
			c.Comment++
		}
	}
	return c
}

// scanState carries string/comment context across line breaks.
type scanState struct{ inBlock, inRaw bool }

// scanLine reports whether the line opens with a comment (or sits inside a block comment) and returns the
// state for the next line. It steps over string, rune and raw-string literals.
func scanLine(line string, st scanState) (bool, scanState) {
	comment := st.inBlock
	i := 0
	if !st.inBlock && !st.inRaw {
		for i < len(line) && (line[i] == ' ' || line[i] == '\t') {
			i++
		}
		if strings.HasPrefix(line[i:], "//") {
			return true, st
		}
		comment = strings.HasPrefix(line[i:], "/*")
	}
	for i < len(line) {
		switch {
		case st.inBlock, st.inRaw:
			term := "*/"
			if st.inRaw {
				term = "`"
			}
			end := strings.Index(line[i:], term)
			if end < 0 {
				return comment, st
			}
			st.inBlock, st.inRaw, i = false, false, i+end+len(term)
		case strings.HasPrefix(line[i:], "//"):
			return comment, st
		case strings.HasPrefix(line[i:], "/*"):
			st.inBlock, i = true, i+2
		case line[i] == '`':
			st.inRaw, i = true, i+1
		case line[i] == '"' || line[i] == '\'':
			i = skipQuoted(line, i)
		default:
			i++
		}
	}
	return comment, st
}

// skipQuoted returns the index past the literal opened at i (len(line) if unterminated).
func skipQuoted(line string, i int) int {
	for j := i + 1; j < len(line); j++ {
		if line[j] == '\\' {
			j++
		} else if line[j] == line[i] {
			return j + 1
		}
	}
	return len(line)
}

// Unlisted files stay under the cap, listed files never grow; "exceptions"
// (generated bundles, byte-pinned twins) sit outside both.
func TestFileSizeBudget(t *testing.T) {
	b, files := load(t)
	for _, path := range sortedKeys(files) {
		if _, skipped := b.Exceptions[path]; skipped {
			continue
		}
		total := files[path].Total
		recorded, listed := b.Files[path]
		switch {
		case !listed && total > b.MaxNewFileLines:
			t.Errorf("%s is %d lines, over the %d-line cap for unlisted files; split it or add it to sizebudget.json", path, total, b.MaxNewFileLines)
		case listed && total > recorded:
			t.Errorf("%s grew to %d lines, budget is %d; shrink it instead of raising the number", path, total, recorded)
		case listed && total <= recorded*9/10:
			t.Logf("%s shrank to %d lines (budget %d) — ratchet sizebudget.json down", path, total, recorded)
		}
	}
}

// No directory's comment share may rise above its recorded percent.
func TestCommentShareBudget(t *testing.T) {
	b, files := load(t)
	dirs := map[string]counts{}
	for path, c := range files {
		dir := filepath.ToSlash(filepath.Dir(path))
		agg := dirs[dir]
		agg.Total, agg.Comment = agg.Total+c.Total, agg.Comment+c.Comment
		dirs[dir] = agg
	}
	for _, dir := range sortedKeys(b.CommentPct) {
		if got := pct(dirs[dir]); got > b.CommentPct[dir] {
			t.Errorf("%s comment share is %d%%, budget is %d%%", dir, got, b.CommentPct[dir])
		}
	}
}

func pct(c counts) int {
	if c.Total == 0 {
		return 0
	}
	return c.Comment * 100 / c.Total
}

func sortedKeys[V any](m map[string]V) []string {
	keys := make([]string, 0, len(m))
	for k := range m {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	return keys
}
