package validate

// The two numbers a client states that the store keeps as int4: an upload's pixel size and a write's
// expected version. Checked before any byte moves, so an impossible value is a 400, never a store error.

import (
	"errors"
	"math"
	"strconv"
)

// MaxImageSide bounds an upload's stated width and height: past every client's decoder (16384) and
// canvas limit, far under int4.
const MaxImageSide = 65535

// ImageSize parses an upload's ?w= and ?h=; an absent value is 0, "unknown".
func ImageSize(rawW, rawH string) (w, h int, err error) {
	if w, err = side("w", rawW); err != nil {
		return 0, 0, err
	}
	h, err = side("h", rawH)
	return w, h, err
}

func side(name, raw string) (int, error) {
	if raw == "" {
		return 0, nil
	}
	n, err := strconv.Atoi(raw)
	if err != nil || n < 0 || n > MaxImageSide {
		return 0, errors.New(name + " must be an integer 0.." + strconv.Itoa(MaxImageSide))
	}
	return n, nil
}

// Version refuses an expected version no row can hold: the column is int4.
func Version(v int64) error {
	if v < 0 || v > math.MaxInt32 {
		return errors.New("version must be 0.." + strconv.Itoa(math.MaxInt32))
	}
	return nil
}
