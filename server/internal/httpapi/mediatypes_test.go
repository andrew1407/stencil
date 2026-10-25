package httpapi

import (
	"bytes"
	"os"
	"path/filepath"
	"runtime"
	"testing"
)

// Drift check against canonical common/config/mediaTypes.json: the embedded copy must stay byte-identical.
func TestMediaTypesAssetPinsCanonical(t *testing.T) {
	_, thisFile, _, ok := runtime.Caller(0)
	if !ok {
		t.Fatal("runtime.Caller failed")
	}
	assetPath := filepath.Join(filepath.Dir(thisFile), "..", "..", "..", "common", "config", "mediaTypes.json")
	raw, err := os.ReadFile(assetPath)
	if os.IsNotExist(err) {
		t.Skipf("canonical asset %s not present (standalone server checkout)", assetPath)
	}
	if err != nil {
		t.Fatalf("read canonical asset: %v", err)
	}
	if !bytes.Equal(raw, mediaTypesAsset) {
		t.Errorf("assets/mediaTypes.json differs from %s (%d vs %d bytes) — run go generate ./...", assetPath, len(mediaTypesAsset), len(raw))
	}
}

// The types the server served before the table drove them, and nothing a browser would run.
func TestContentTypeForExtServesTheTablesImagesAndJSON(t *testing.T) {
	want := map[string]string{
		".png": "image/png", ".jpg": "image/jpeg", ".JPEG": "image/jpeg", "webp": "image/webp",
		".gif": "image/gif", ".bmp": "image/bmp", ".json": "application/json",
		".svg": "application/octet-stream", ".html": "application/octet-stream", ".avif": "application/octet-stream",
		"": "application/octet-stream",
	}
	for ext, typ := range want {
		if got := contentTypeForExt(ext); got != typ {
			t.Errorf("contentTypeForExt(%q) = %q, want %q", ext, got, typ)
		}
	}
}
