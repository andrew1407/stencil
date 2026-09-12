package transport

// The transport-neutral close codes both adapters share.

import (
	"testing"
)

// Close codes are a transport-neutral set the WS adapter maps to RFC6455 status
// codes; they must stay inside the range that mapping is valid for.
func TestCloseCodesAreValidRFC6455StatusCodes(t *testing.T) {
	for name, code := range map[string]int{
		"CloseNormal": CloseNormal, "ClosePolicyViolation": ClosePolicyViolation,
	} {
		if code < 1000 || code > 4999 {
			t.Errorf("%s = %d, outside the RFC6455 status range", name, code)
		}
	}
	if CloseNormal != 1000 || ClosePolicyViolation != 1008 {
		t.Error("close codes drifted from the RFC6455 values clients expect")
	}
}
