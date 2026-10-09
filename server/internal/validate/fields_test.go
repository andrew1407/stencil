package validate

import (
	"testing"

	"stencil/server/internal/protocol"
)

// A create states its image size as an upload does: 0..MaxImageSide, so an int4 overflow is a 400.
func TestCreateProjectBoundsTheImageSize(t *testing.T) {
	for _, tc := range []struct {
		w, h int
		ok   bool
	}{{0, 0, true}, {MaxImageSide, MaxImageSide, true}, {-1, 1, false}, {1, MaxImageSide + 1, false}, {1 << 40, 1, false}} {
		err := CreateProject(protocol.CreateProjectRequest{ImageW: tc.w, ImageH: tc.h})
		if (err == nil) != tc.ok {
			t.Errorf("imageW %d imageH %d: err %v, want ok=%v", tc.w, tc.h, err, tc.ok)
		}
	}
}
