package validate

// The paging bound, at its edges. A limit that is refused rather than silently
// cut is what lets a client trust the page it asked for.

import "testing"

func TestListLimitBounds(t *testing.T) {
	for _, tc := range []struct {
		raw     string
		want    int
		wantErr bool
	}{
		{"", 0, false}, // absent = every project
		{"1", 1, false},
		{"500", MaxListLimit, false},
		{"0", 0, true},
		{"-1", 0, true},
		{"501", 0, true},
		{"abc", 0, true},
		{"1.5", 0, true},
	} {
		got, err := ListLimit(tc.raw)
		if (err != nil) != tc.wantErr {
			t.Errorf("ListLimit(%q) err = %v, want error=%v", tc.raw, err, tc.wantErr)
			continue
		}
		if got != tc.want {
			t.Errorf("ListLimit(%q) = %d, want %d", tc.raw, got, tc.want)
		}
	}
}

// The refusal names the range, so a client can correct the request.
func TestListLimitErrorNamesTheRange(t *testing.T) {
	_, err := ListLimit("9999")
	if err == nil || err.Error() != "limit must be 1..500" {
		t.Fatalf("err = %v, want the 1..500 message", err)
	}
}
