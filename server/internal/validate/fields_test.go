package validate

import (
	"encoding/json"
	"os"
	"path/filepath"
	"runtime"
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

// A layout is opaque but bounded, on create and update alike; a negative expiry is refused.
func TestLayoutIsCappedAndExpiryNonNegative(t *testing.T) {
	big := make([]byte, MaxLayoutBytes+1)
	fits := make([]byte, MaxLayoutBytes)
	if CreateProject(protocol.CreateProjectRequest{Layout: big}) == nil {
		t.Error("an over-cap layout was accepted on create")
	}
	if err := CreateProject(protocol.CreateProjectRequest{Layout: fits}); err != nil {
		t.Errorf("a layout at the cap was refused: %v", err)
	}
	if UpdateProject(protocol.UpdateProjectRequest{Layout: big, Version: 1}) == nil {
		t.Error("an over-cap layout was accepted on update")
	}
	neg, zero := int64(-1), int64(0)
	if CreateProject(protocol.CreateProjectRequest{ExpiresAt: -5}) == nil ||
		UpdateProject(protocol.UpdateProjectRequest{ExpiresAt: &neg, Version: 1}) == nil {
		t.Error("a negative expiresAt was accepted")
	}
	if err := UpdateProject(protocol.UpdateProjectRequest{ExpiresAt: &zero, Version: 1}); err != nil {
		t.Errorf("expiresAt 0 (keep forever) was refused: %v", err)
	}
}

// The cap is sized to the shared limit (common/config/constants.json LIMITS.layoutPointsMax), so a
// layout any client may legitimately draw always fits.
func TestLayoutCapFitsTheSharedPointLimit(t *testing.T) {
	_, thisFile, _, _ := runtime.Caller(0)
	raw, err := os.ReadFile(filepath.Join(filepath.Dir(thisFile), "../../../common/config/constants.json"))
	if os.IsNotExist(err) {
		t.Skip("common/config/constants.json not present (standalone server checkout)")
	}
	if err != nil {
		t.Fatal(err)
	}
	var c struct {
		LIMITS struct {
			LayoutPointsMax int `json:"layoutPointsMax"`
		} `json:"LIMITS"`
	}
	if err := json.Unmarshal(raw, &c); err != nil || c.LIMITS.LayoutPointsMax <= 0 {
		t.Fatalf("read LIMITS.layoutPointsMax: %v", err)
	}
	const pointBytes = len(`{"x":1234.5678901234567,"y":8765.432109876543},`)
	if need := c.LIMITS.LayoutPointsMax * pointBytes; MaxLayoutBytes < need {
		t.Fatalf("MaxLayoutBytes %d is below %d points × %d bytes = %d", MaxLayoutBytes, c.LIMITS.LayoutPointsMax, pointBytes, need)
	}
}
