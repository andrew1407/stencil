package httpapi

import (
	"fmt"
	"net/http"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"

	"stencil/server/internal/testutil"
)

// Byte-exact goldens for the server's pinned LLM text surface: the two system-prompt
// heads and the GET /llm/info body. Both are slated to move into JSON assets, so these
// pin the current bytes to prove the move is verbatim. Rewrite with
// SERVER_UPDATE_GOLDENS=1 go test ./internal/httpapi/...

// goldenPath resolves tests/goldens beside this file, the way llmprompt_test.go finds
// the canonical asset.
func goldenPath(name string) string {
	_, thisFile, _, ok := runtime.Caller(0)
	if !ok {
		panic("runtime.Caller failed")
	}
	return filepath.Join(filepath.Dir(thisFile), "goldens", name)
}

func checkGolden(t *testing.T, name, actual string) {
	t.Helper()
	path := goldenPath(name)
	if os.Getenv("SERVER_UPDATE_GOLDENS") == "1" {
		if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
			t.Fatalf("create the goldens dir: %v", err)
		}
		if err := os.WriteFile(path, []byte(actual), 0o644); err != nil {
			t.Fatalf("write %s: %v", path, err)
		}
		return
	}
	raw, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read %s: %v — rerun with SERVER_UPDATE_GOLDENS=1", path, err)
	}
	if string(raw) == actual {
		return
	}
	t.Errorf("golden %s differs (SERVER_UPDATE_GOLDENS=1 to rewrite):\n%s",
		name, goldenDiff(string(raw), actual))
}

// goldenDiff reports the first few differing lines, so a failure names the wording
// that moved rather than just saying "not equal".
func goldenDiff(expected, actual string) string {
	want := strings.Split(expected, "\n")
	got := strings.Split(actual, "\n")
	n := len(want)
	if len(got) > n {
		n = len(got)
	}
	var b strings.Builder
	shown := 0
	for i := 0; i < n && shown < 12; i++ {
		w, g := "<eof>", "<eof>"
		if i < len(want) {
			w = want[i]
		}
		if i < len(got) {
			g = got[i]
		}
		if w == g {
			continue
		}
		fmt.Fprintf(&b, "  line %d:\n    want %s\n    got  %s\n", i+1, w, g)
		shown++
	}
	if shown == 0 {
		fmt.Fprintf(&b, "  identical line-wise; lengths %d vs %d\n", len(expected), len(actual))
	}
	return b.String()
}

func TestPromptHeadGoldens(t *testing.T) {
	checkGolden(t, "llm_editor_prompt_head.txt", llmEditorPromptHead)
	checkGolden(t, "llm_extension_prompt_head.txt", llmExtensionPromptHead)
}

func TestLLMInfoBodyGolden(t *testing.T) {
	var b strings.Builder
	for _, tc := range []struct {
		name string
		llm  LLM
	}{
		{"disabled", nil},
		{"enabled (model claude-test)", &testutil.EchoLLM{}},
	} {
		api := llmAPI(t, tc.llm)
		tok := issueToken(t, api, "")
		rec := do(t, api, http.MethodGet, "/llm/info", tok, nil)
		fmt.Fprintf(&b, "== GET /llm/info — %s\n%d %s\n%s\n",
			tc.name, rec.Code, rec.Header().Get("Content-Type"), rec.Body.String())
	}
	checkGolden(t, "llm_info.txt", b.String())
}
